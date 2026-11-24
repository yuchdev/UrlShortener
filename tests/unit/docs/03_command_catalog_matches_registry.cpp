#define BOOST_TEST_MODULE DocsCommandCatalogMatchesRegistry
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/builtin_commands.hpp"
#include "url_shortener/commands/catalog.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/composition/command_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/core/config.hpp"

using url_shortener::commands::all_error_infos;
using url_shortener::commands::ArgType;
using url_shortener::commands::catalog_begin_marker;
using url_shortener::commands::catalog_end_marker;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::error_code_from_name;
using url_shortener::commands::error_info_for;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::JobInfo;
using url_shortener::commands::make_error;
using url_shortener::commands::make_failure;
using url_shortener::commands::make_job_accepted;
using url_shortener::commands::make_success;
using url_shortener::commands::parse_request;
using url_shortener::commands::Permission;
using url_shortener::commands::permission_name;
using url_shortener::commands::serialize_response;
using url_shortener::composition::build_command_runtime;
using url_shortener::composition::CommandRuntime;

namespace
{

// ===========================================================================
// Bounded, single-pass Markdown helpers. Deliberately no std::regex (and no
// nested quantifiers): every scan below is a linear walk with explicit bounds
// so the parser cannot blow up on hostile input (it never sees hostile input
// here, but the discipline keeps it auditable).
// ===========================================================================

/// Replaces every CRLF with a single LF so a Windows (autocrlf) checkout of
/// `docs/commands/README.md` is compared on the same footing as an LF checkout.
std::string normalize_crlf(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        out += text[i];
    }
    return out;
}

/// Reads an entire file in binary mode.
/// @param path Absolute path to read.
/// @return The file bytes on success; `std::nullopt` when the file cannot be
/// opened (the caller turns that into a skip or a required-failure).
std::optional<std::string> read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Trims ASCII spaces and tabs from both ends of a view.
std::string trim(std::string_view s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        --end;
    }
    return std::string(s.substr(begin, end - begin));
}

/// Splits @p text into lines on `\n`; a trailing newline does not produce a
/// final empty line.
std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        }
        else {
            current += c;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

/// Extracts every backtick-delimited token from @p line, in order.
///
/// A token is the text strictly between a pair of backticks; an unterminated
/// backtick at end of line yields no token. This is how command names, the
/// `Perm` value and enum members are lifted out of a prose/table cell without
/// depending on any surrounding punctuation.
std::vector<std::string> backtick_tokens(std::string_view line)
{
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] != '`') {
            ++i;
            continue;
        }
        const std::size_t close = line.find('`', i + 1);
        if (close == std::string_view::npos) {
            break;
        }
        tokens.emplace_back(line.substr(i + 1, close - (i + 1)));
        i = close + 1;
    }
    return tokens;
}

/// Splits one Markdown table row into trimmed cells, treating a backslash-pipe
/// (`\|`, the GitHub escape used inside enum type cells like
/// ``enum `csv`\|`ndjson` ``) as a literal, never a column separator.
///
/// The leading and trailing empty cells produced by the outer `|` delimiters
/// are dropped, so `| a | b |` yields `{"a", "b"}`.
std::vector<std::string> table_cells(std::string_view row)
{
    std::vector<std::string> cells;
    std::string current;
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (row[i] == '\\' && i + 1 < row.size() && row[i + 1] == '|') {
            current += '|';
            ++i;
            continue;
        }
        if (row[i] == '|') {
            cells.push_back(trim(current));
            current.clear();
            continue;
        }
        current += row[i];
    }
    cells.push_back(trim(current));
    if (!cells.empty() && cells.front().empty()) {
        cells.erase(cells.begin());
    }
    if (!cells.empty() && cells.back().empty()) {
        cells.pop_back();
    }
    return cells;
}

/// True when @p line is a Markdown table separator row such as `|---|---|`.
bool is_separator_row(const std::string& line)
{
    const std::string t = trim(line);
    if (t.empty() || t.front() != '|') {
        return false;
    }
    for (const char c : t) {
        if (c != '|' && c != '-' && c != ' ' && c != ':') {
            return false;
        }
    }
    return true;
}

/// True when @p line opens a Markdown heading (`#`-prefixed) of any level.
bool is_heading(const std::string& line)
{
    return !line.empty() && line.front() == '#';
}

// ===========================================================================
// Parsed-doc model.
// ===========================================================================

/// The command metadata the hand-written sections 1-6 assert for one command.
///
/// `from_heading` distinguishes a command defined by a `#### ...` subsection in
/// sections 3.1-3.3 (which states `Perm`, `Kind`, `Mut` and `dry_run`) from one
/// defined only by a row in the 3.4/3.5 `Command | Perm | ...` tables (which
/// state the permission alone).
struct DocCommand
{
    ///< The `Perm` value (first backticked token of the heading meta or the
    ///< Perm column), e.g. `"read"`.
    std::string perm;

    ///< `"sync"` or `"job"` as read from the heading meta; empty for a
    ///< table-defined command, which does not state a kind.
    std::string kind;

    ///< `true` when the heading meta carries the `Mut` flag; always `false` for
    ///< a table-defined command.
    bool mutating = false;

    ///< `true` when the heading meta carries `` `dry_run` ``; always `false`
    ///< for a table-defined command.
    bool dry_run = false;

    ///< `true` when the heading meta carries a `` (`write` with ...) ``
    ///< permission parenthetical (the argument-dependent `permission_for`
    ///< annotation: D16 `path` on `link.export`, T-71 `status` on
    ///< `link.list`/`link.export`).
    bool has_perm_parenthetical = false;

    ///< `true` when this command came from a `#### ` subsection heading.
    bool from_heading = false;
};

/// One row of a hand-written `| Arg | Type | Req | Description |` table.
struct DocArg
{
    ///< The lower-cased `Req` cell: `"yes"`, `"no"` or `"one of"`.
    std::string req;

    ///< The raw `Type` cell text (used only to pull enum members when it begins
    ///< with `enum`).
    std::string type;
};

/// Everything the drift checks need parsed out of `docs/commands/README.md`.
struct ParsedCatalog
{
    ///< Count of `catalog_begin_marker` occurrences (must be exactly one).
    std::size_t begin_markers = 0;

    ///< Count of `catalog_end_marker` occurrences (must be exactly one).
    std::size_t end_markers = 0;

    ///< Documented command metadata keyed by command name (sections 3.1-3.5,
    ///< generated block excluded).
    std::map<std::string, DocCommand> commands;

    ///< Documented argument tables keyed by command name; each value preserves
    ///< the table's row order.
    std::map<std::string, std::vector<std::pair<std::string, DocArg>>> args;

    ///< Error table rows keyed by code name: {http_status, cli_exit}.
    std::map<std::string, std::pair<unsigned, int>> error_rows;

    ///< `"key":`-style field names found in the section-1 Request JSON block.
    std::set<std::string> request_fields;

    ///< `"key":`-style field names found in the section-1 Response JSON blocks.
    std::set<std::string> response_fields;
};

/// Extracts the `"<name>":` field keys from a JSON-ish text span.
///
/// Scans for a double-quoted token immediately followed (allowing spaces) by a
/// colon, i.e. an object key. Bounded linear walk; values and nested strings
/// that are not keys are ignored.
void collect_json_keys(std::string_view text, std::set<std::string>& out)
{
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '"') {
            ++i;
            continue;
        }
        const std::size_t close = text.find('"', i + 1);
        if (close == std::string_view::npos) {
            break;
        }
        std::size_t j = close + 1;
        while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) {
            ++j;
        }
        if (j < text.size() && text[j] == ':') {
            out.emplace(text.substr(i + 1, close - (i + 1)));
        }
        i = close + 1;
    }
}

/// Parses the hand-written sections of @p markdown into a @ref ParsedCatalog.
///
/// Single pass over the lines. The region between (and including) the generated
/// begin/end markers is never mined for headings or command tables, so block
/// 3.7 (owned by `commands__12_catalog_drift`) is not double-asserted here.
ParsedCatalog parse_catalog(const std::string& markdown)
{
    ParsedCatalog parsed;

    // Marker counts (byte scan, independent of line structure).
    for (std::size_t pos = markdown.find(catalog_begin_marker);
         pos != std::string::npos;
         pos = markdown.find(catalog_begin_marker, pos + 1))
    {
        ++parsed.begin_markers;
    }
    for (std::size_t pos = markdown.find(catalog_end_marker);
         pos != std::string::npos;
         pos = markdown.find(catalog_end_marker, pos + 1))
    {
        ++parsed.end_markers;
    }

    const std::vector<std::string> lines = split_lines(markdown);

    // Generated-region line bounds (inclusive). Lines inside are skipped.
    std::size_t gen_begin = lines.size();
    std::size_t gen_end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(catalog_begin_marker) != std::string::npos) {
            gen_begin = i;
        }
        if (lines[i].find(catalog_end_marker) != std::string::npos) {
            gen_end = i;
        }
    }
    const auto in_generated = [&](std::size_t i)
    { return i >= gen_begin && i <= gen_end; };

    // Request/response field sets from the section-1 JSON code blocks. The
    // Request block lives between "### Request" and "### Response"; the
    // Response blocks between "### Response" and "### Error codes".
    std::size_t req_line = lines.size();
    std::size_t resp_line = lines.size();
    std::size_t err_line = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (req_line == lines.size() && t == "### Request") {
            req_line = i;
        }
        else if (resp_line == lines.size() && t == "### Response") {
            resp_line = i;
        }
        else if (err_line == lines.size() && t == "### Error codes") {
            err_line = i;
        }
    }
    if (req_line < resp_line && resp_line <= lines.size()) {
        for (std::size_t i = req_line + 1; i < resp_line; ++i) {
            collect_json_keys(lines[i], parsed.request_fields);
        }
    }
    if (resp_line < err_line && err_line <= lines.size()) {
        for (std::size_t i = resp_line + 1; i < err_line; ++i) {
            collect_json_keys(lines[i], parsed.response_fields);
        }
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (in_generated(i)) {
            continue;
        }
        const std::string& line = lines[i];
        const std::string t = trim(line);

        // ---- Command subsection heading (sections 3.1-3.3) ----
        if (t.rfind("#### ", 0) == 0) {
            const std::string body = t.substr(5);
            const std::size_t dash = body.find(" - ");
            if (dash == std::string::npos) {
                continue;
            }
            const std::string names_part = body.substr(0, dash);
            const std::string meta = body.substr(dash + 3);

            const std::vector<std::string> name_tokens =
                backtick_tokens(names_part);
            const std::vector<std::string> meta_tokens = backtick_tokens(meta);

            DocCommand dc;
            dc.from_heading = true;
            dc.perm = meta_tokens.empty() ? std::string() : meta_tokens.front();
            dc.kind = (meta.find("job") != std::string::npos) ? "job" : "sync";
            dc.mutating = meta.find("Mut") != std::string::npos;
            dc.dry_run = meta.find("dry_run") != std::string::npos;
            // Any argument-dependent permission raise is annotated with the
            // fixed `` (`write` with <arg>...) `` form; recognize the whole
            // family, not just the `path` case (D16 path, T-71 status).
            dc.has_perm_parenthetical =
                meta.find("(`write` with") != std::string::npos;

            std::vector<std::string> heading_names;
            for (const std::string& tok : name_tokens) {
                if (tok.find('.') != std::string::npos) {
                    parsed.commands[tok] = dc;
                    heading_names.push_back(tok);
                }
            }

            // Look ahead within this subsection for an argument table.
            std::vector<std::pair<std::string, DocArg>> rows;
            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                if (in_generated(j) || is_heading(lines[j])) {
                    break;
                }
                const std::vector<std::string> header = table_cells(lines[j]);
                if (header.size() == 4 && header[0] == "Arg"
                    && header[1] == "Type" && header[2] == "Req")
                {
                    for (std::size_t k = j + 2; k < lines.size(); ++k) {
                        const std::string rt = trim(lines[k]);
                        if (rt.empty() || rt.front() != '|') {
                            break;
                        }
                        const std::vector<std::string> cells =
                            table_cells(lines[k]);
                        if (cells.size() < 3) {
                            continue;
                        }
                        DocArg arg;
                        std::string req = cells[2];
                        std::transform(
                            req.begin(),
                            req.end(),
                            req.begin(),
                            [](unsigned char c)
                            { return static_cast<char>(std::tolower(c)); });
                        arg.req = req;
                        arg.type = cells[1];
                        for (const std::string& an : backtick_tokens(cells[0]))
                        {
                            rows.emplace_back(an, arg);
                        }
                    }
                    break;
                }
            }
            if (!rows.empty()) {
                for (const std::string& hn : heading_names) {
                    parsed.args[hn] = rows;
                }
            }
            continue;
        }

        // ---- Tables (`| ... |` header rows outside the generated block) ----
        if (t.rfind("| ", 0) != 0) {
            continue;
        }
        const std::vector<std::string> header = table_cells(line);

        // Command | Perm | ... tables (sections 3.4 job, 3.5 system).
        std::size_t cmd_col = header.size();
        std::size_t perm_col = header.size();
        for (std::size_t c = 0; c < header.size(); ++c) {
            if (header[c] == "Command") {
                cmd_col = c;
            }
            else if (header[c] == "Perm") {
                perm_col = c;
            }
        }
        if (cmd_col < header.size() && perm_col < header.size()) {
            for (std::size_t k = i + 2; k < lines.size(); ++k) {
                const std::string rt = trim(lines[k]);
                if (rt.empty() || rt.front() != '|') {
                    break;
                }
                const std::vector<std::string> cells = table_cells(lines[k]);
                if (cells.size() <= std::max(cmd_col, perm_col)) {
                    continue;
                }
                const std::vector<std::string> cmd_names =
                    backtick_tokens(cells[cmd_col]);
                const std::vector<std::string> perm_names =
                    backtick_tokens(cells[perm_col]);
                if (cmd_names.empty()
                    || cmd_names.front().find('.') == std::string::npos)
                {
                    continue;
                }
                DocCommand dc;
                dc.from_heading = false;
                dc.perm =
                    perm_names.empty() ? std::string() : perm_names.front();
                parsed.commands[cmd_names.front()] = dc;
            }
            continue;
        }

        // Error-code table (section 1).
        std::size_t code_col = header.size();
        std::size_t http_col = header.size();
        std::size_t exit_col = header.size();
        for (std::size_t c = 0; c < header.size(); ++c) {
            if (header[c] == "Code") {
                code_col = c;
            }
            else if (header[c] == "HTTP") {
                http_col = c;
            }
            else if (header[c].find("CLI exit") != std::string::npos) {
                exit_col = c;
            }
        }
        if (code_col < header.size() && http_col < header.size()
            && exit_col < header.size())
        {
            for (std::size_t k = i + 2; k < lines.size(); ++k) {
                const std::string rt = trim(lines[k]);
                if (rt.empty() || rt.front() != '|') {
                    break;
                }
                const std::vector<std::string> cells = table_cells(lines[k]);
                if (cells.size() <= std::max({code_col, http_col, exit_col})) {
                    continue;
                }
                const std::vector<std::string> code_names =
                    backtick_tokens(cells[code_col]);
                if (code_names.empty()) {
                    continue;
                }
                // Strip emphasis / non-digits (handles `**409**`).
                std::string http_digits;
                for (const char ch : cells[http_col]) {
                    if (ch >= '0' && ch <= '9') {
                        http_digits += ch;
                    }
                }
                std::string exit_digits;
                for (const char ch : cells[exit_col]) {
                    if (ch >= '0' && ch <= '9') {
                        exit_digits += ch;
                    }
                }
                if (http_digits.empty() || exit_digits.empty()) {
                    continue;
                }
                parsed.error_rows[code_names.front()] = {
                    static_cast<unsigned>(std::stoul(http_digits)),
                    std::stoi(exit_digits)};
            }
            continue;
        }
    }

    return parsed;
}

/// Parses the two `{ "code": N, ... }` dict literals `HTTP_STATUS` and
/// `CLI_EXIT` out of the conformance Python common module.
///
/// Bounded, format-tolerant: it finds a line opening the named dict and then
/// reads `"<name>": <int>` entries until the closing `}`. Entries it cannot
/// parse are skipped rather than failing, so the cross-check stays robust while
/// Task 07.0 subtask 05 continues to evolve that file.
///
/// @param py The whole Python source.
/// @param dict_name `"HTTP_STATUS"` or `"CLI_EXIT"`.
/// @return name -> integer value for every entry that parsed.
std::map<std::string, int> parse_python_int_dict(const std::string& py,
                                                 std::string_view dict_name)
{
    std::map<std::string, int> out;
    const std::vector<std::string> lines = split_lines(py);
    std::size_t start = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (t.rfind(std::string(dict_name), 0) == 0
            && t.find('{') != std::string::npos)
        {
            start = i;
            break;
        }
    }
    if (start == lines.size()) {
        return out;
    }
    for (std::size_t i = start + 1; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (t.rfind('}', 0) == 0) {
            break;
        }
        if (t.empty() || t.front() != '"') {
            continue;
        }
        const std::size_t close = t.find('"', 1);
        if (close == std::string::npos) {
            continue;
        }
        const std::string key = t.substr(1, close - 1);
        std::string digits;
        bool seen_colon = false;
        for (std::size_t c = close + 1; c < t.size(); ++c) {
            if (t[c] == ':') {
                seen_colon = true;
                continue;
            }
            if (seen_colon && t[c] >= '0' && t[c] <= '9') {
                digits += t[c];
            }
            else if (!digits.empty()) {
                break;
            }
        }
        if (!digits.empty()) {
            out[key] = std::stoi(digits);
        }
    }
    return out;
}

/// Maps an @ref ArgType to `true` when it is the enumeration type.
bool is_enum_type(ArgType type)
{
    return type == ArgType::enumeration;
}

/// Hint appended to every descriptor-sourced failure message: the live values
/// come from the command registry, so run the catalog dump to see them and then
/// fix the hand-written prose by hand (only block 3.7 is generator-owned).
constexpr const char* k_regen_hint =
    " (run ./cmake-build/command_catalog_dump to print the live descriptor "
    "metadata, then fix the prose in docs/commands/README.md by hand)";

/// Maps a hand-written `Type`-cell's leading base token to its @ref ArgType.
///
/// Only the leading run of lower-case ASCII letters is read (`int 0..1000000`
/// -> `int`, `enum `a`\|`b`` -> `enum`, `object `{...}` | `null`` -> `object`),
/// which is the clean catalog vocabulary. A cell whose base token is not one of
/// the recognized spellings yields `std::nullopt`, which the caller turns into
/// a failure so a new, unrecognized type spelling cannot pass silently.
std::optional<ArgType> doc_base_type(std::string_view type_cell)
{
    std::size_t i = 0;
    const std::string t = trim(type_cell);
    std::string token;
    while (i < t.size() && t[i] >= 'a' && t[i] <= 'z') {
        token += t[i];
        ++i;
    }
    if (token == "string") {
        return ArgType::string;
    }
    if (token == "int" || token == "integer") {
        return ArgType::integer;
    }
    if (token == "bool" || token == "boolean") {
        return ArgType::boolean;
    }
    if (token == "enum") {
        return ArgType::enumeration;
    }
    if (token == "timestamp") {
        return ArgType::timestamp;
    }
    if (token == "list" || token == "array") {
        return ArgType::string_list;
    }
    if (token == "map") {
        return ArgType::string_map;
    }
    if (token == "object") {
        return ArgType::object;
    }
    return std::nullopt;
}

/// `command.arg` keys whose hand-written `Type` cell is genuinely irregular and
/// is therefore exempt from the base-type comparison (names, required and clean
/// enum members are still checked). Every entry carries its reason; a NEW
/// irregular cell is NOT silently skipped - it fails @ref argspec_tables_match
/// until it is added here on purpose.
const std::set<std::string>& type_comparison_exceptions()
{
    static const std::set<std::string> exceptions = {
        // The 3.2 db.rows table folds two arguments of different types into one
        // combined `` `sort` / `order` `` row with a single `string / enum`
        // Type cell, so neither sub-cell can be attributed to one ArgType.
        "db.rows.sort",
        "db.rows.order",
    };
    return exceptions;
}

/// The one production registry builder, over the default (in-memory/legacy)
/// backend, plus the committed catalog text and its parse.
///
/// Lifetime: @ref runtime owns the registry and every object its handlers
/// reference; @ref registry is a non-owning pointer into it and is valid for
/// the fixture's lifetime. The handlers are never invoked - only descriptor
/// metadata is read.
struct CatalogFixture
{
    CommandRuntime runtime;
    const CommandRegistry* registry = nullptr;
    bool loaded = false;
    std::string source_dir;
    ParsedCatalog doc;

    CatalogFixture()
    {
        url_shortener::app::LinkBackend::reset();
        ServerConfig server;
        url_shortener::app::LinkBackendHandle backend;  // inactive / legacy
        runtime = build_command_runtime(server, backend);
        registry = runtime.registry.get();

#ifdef SOURCE_DIR
        source_dir = SOURCE_DIR;
        const auto raw = read_file(source_dir + "/docs/commands/README.md");
        if (raw.has_value()) {
            doc = parse_catalog(normalize_crlf(*raw));
            loaded = true;
        }
#endif
    }

    /// Non-owning registry descriptor pointers keyed by name.
    std::map<std::string, const CommandDescriptor*> descriptors() const
    {
        std::map<std::string, const CommandDescriptor*> out;
        for (const CommandDescriptor* d : registry->list()) {
            out[d->name] = d;
        }
        return out;
    }
};

}  // namespace

// Type: unit (documentation-drift guard, bounded in-process parse).
// Scenario: the hand-written sections 1-6 of docs/commands/README.md are parsed
//   once and compared against the live CommandRegistry that
//   composition::build_command_runtime assembles the way production does, so a
//   drift in either the prose or a descriptor fails the unit suite with a
//   message naming the section, command and field.
// Boundaries: the generated block 3.7 (owned by commands__12_catalog_drift) is
//   excluded from every scan; CRLF checkouts are normalized; no network, no
//   process spawn; handlers are never invoked.
// On failure first check: did a descriptor field change without updating the
//   matching prose row (fix docs/commands/README.md by hand), or did a prose
//   edit diverge from the registry? This test never rewrites either side.

BOOST_FIXTURE_TEST_SUITE(catalog_drift, CatalogFixture)

// The fixture must find and parse the committed catalog, and the production
// registry must be non-empty, so no case below can pass vacuously.
BOOST_AUTO_TEST_CASE(fixture_loaded_and_registry_nonempty)
{
#ifndef SOURCE_DIR
    BOOST_TEST_MESSAGE("SOURCE_DIR not defined; run via CTest. Skipping.");
#else
    BOOST_REQUIRE_MESSAGE(
        loaded, "could not read docs/commands/README.md under " << source_dir);
    BOOST_REQUIRE(registry != nullptr);
    BOOST_REQUIRE_GT(registry->list().size(), 0u);
#endif
}

// Scenario: the catalog carries exactly one begin/end generated-marker pair, so
//   the byte-for-byte block-3.7 comparison in commands__12_catalog_drift has a
//   single, well-formed region to own.
// On failure first check: were the generated markers duplicated or dropped when
//   editing the prose around block 3.7?
BOOST_AUTO_TEST_CASE(generated_block_present)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    BOOST_TEST(doc.begin_markers == 1u,
               "expected exactly one catalog begin marker");
    BOOST_TEST(doc.end_markers == 1u,
               "expected exactly one catalog end marker");
#endif
}

// Scenario: every registered descriptor is documented by a #### heading
//   (sections 3.1-3.3) or a Command|Perm table row (3.4/3.5), and no documented
//   command is absent from the registry.
// Boundaries: comparison is by name set; the generated block is excluded.
// On failure first check: a registry-only name means a new command shipped
//   without a prose entry; a doc-only name means a command was removed/renamed
//   without updating the prose.
BOOST_AUTO_TEST_CASE(command_set_matches_registry)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    std::set<std::string> registry_names;
    for (const CommandDescriptor* d : registry->list()) {
        registry_names.insert(d->name);
    }
    std::set<std::string> doc_names;
    for (const auto& [name, meta] : doc.commands) {
        doc_names.insert(name);
    }

    for (const std::string& name : registry_names) {
        BOOST_TEST(doc_names.count(name) == 1u,
                   "section 3: registered command '"
                       << name << "' has no hand-written catalog entry");
    }
    for (const std::string& name : doc_names) {
        BOOST_TEST(registry_names.count(name) == 1u,
                   "section 3: documented command '"
                       << name << "' is not a registered descriptor");
    }
#endif
}

// Scenario: for each documented command the Perm / Kind / Mut / dry_run the
//   prose states equal the live descriptor's base permission, execution kind,
//   mutating flag and dry-run support; table-defined commands (3.4/3.5) assert
//   the permission column alone.
// Boundaries: the base permission is compared (not the per-call D16/T-71
//   raise); kind/mut/dry_run are asserted only for #### headings that state
//   them; the `` (`write` with ...) `` permission-hook annotation must match
//   descriptor.permission_for in BOTH directions (so link.list's T-71 status
//   hook and link.export's D16 path hook are both pinned).
// On failure first check: did a descriptor flip permission/kind/mutating/
//   dry_run or gain/lose a permission_for, or did the heading meta drift from
//   the descriptor? Run command_catalog_dump to see the live values.
BOOST_AUTO_TEST_CASE(perm_kind_mutating_columns_match)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    const auto descs = descriptors();
    for (const auto& [name, meta] : doc.commands) {
        const auto it = descs.find(name);
        if (it == descs.end()) {
            continue;  // reported by command_set_matches_registry
        }
        const CommandDescriptor& d = *it->second;

        BOOST_TEST(meta.perm == std::string(permission_name(d.permission)),
                   "section 3: Perm mismatch for '"
                       << name << "': doc=" << meta.perm << " registry="
                       << permission_name(d.permission) << k_regen_hint);

        if (!meta.from_heading) {
            continue;  // tables state only the permission
        }
        const std::string kind = d.kind == ExecutionKind::job ? "job" : "sync";
        BOOST_TEST(meta.kind == kind,
                   "section 3: Kind mismatch for '"
                       << name << "': doc=" << meta.kind << " registry=" << kind
                       << k_regen_hint);
        BOOST_TEST(
            meta.mutating == d.mutating,
            "section 3: Mut mismatch for '" << name << "'" << k_regen_hint);
        BOOST_TEST(
            meta.dry_run == d.supports_dry_run,
            "section 3: dry_run mismatch for '" << name << "'" << k_regen_hint);

        // The argument-dependent permission hook and its heading parenthetical
        // must agree in BOTH directions: a `` (`write` with ...) `` annotation
        // requires a descriptor.permission_for (D16 path / T-71 status), and a
        // descriptor that carries a permission_for must be annotated so a
        // reader is never surprised by a per-call permission raise.
        BOOST_TEST(
            meta.has_perm_parenthetical == static_cast<bool>(d.permission_for),
            "section 3: '" << name
                           << "' permission-hook annotation disagrees with the "
                              "descriptor's permission_for (doc annotated="
                           << meta.has_perm_parenthetical << ", registry hook="
                           << static_cast<bool>(d.permission_for) << ")"
                           << k_regen_hint);
    }
#endif
}

// Scenario: for every command whose section 3.1-3.3 subsection has a regular
//   | Arg | Type | Req | Description | table, the documented argument names,
//   required-ness and base type match the descriptor's ArgSpec set, and each
//   enumeration argument's documented members (when the Type cell is a clean
//   `enum ...`) match the descriptor's enum_values.
// Boundaries: names are compared as a set; required maps yes->true, no/one of->
//   false; the Type base token (leading word: string/int/bool/enum/timestamp/
//   list/map/object) is compared to ArgType for every cell except the named
//   entries in type_comparison_exceptions (only db.rows's combined sort/order
//   cell today), and an unrecognized token fails; enum members are compared for
//   clean `enum` cells. Commands documented with prose args (link.delete, the
//   enable/disable/restore family, link.preview, db.tables, link.export) are
//   covered by command_set_matches_registry.
// On failure first check: was an argument added/removed/renamed, its
//   required-ness changed, or an enum domain edited on one side only?
BOOST_AUTO_TEST_CASE(argspec_tables_match)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    const auto descs = descriptors();
    for (const auto& [name, rows] : doc.args) {
        const auto it = descs.find(name);
        BOOST_REQUIRE_MESSAGE(
            it != descs.end(),
            "section 3: arg table for unknown command '" << name << "'");
        const CommandDescriptor& d = *it->second;

        std::set<std::string> doc_names;
        std::map<std::string, DocArg> doc_by_name;
        for (const auto& [arg_name, arg] : rows) {
            doc_names.insert(arg_name);
            doc_by_name[arg_name] = arg;
        }
        std::set<std::string> reg_names;
        for (const auto& spec : d.args) {
            reg_names.insert(spec.name);
        }
        BOOST_TEST(doc_names == reg_names,
                   "section 3: argument-name set drift for '" << name << "'"
                                                              << k_regen_hint);

        for (const auto& spec : d.args) {
            const auto da = doc_by_name.find(spec.name);
            if (da == doc_by_name.end()) {
                continue;  // reported by the set comparison above
            }
            const std::string qualified = name + "." + spec.name;
            const bool doc_required = da->second.req == "yes";
            BOOST_TEST(spec.required == doc_required,
                       "section 3: '"
                           << qualified << "' required mismatch (doc req='"
                           << da->second.req << "')" << k_regen_hint);

            // Base-type discriminant. Exempt only the explicitly listed,
            // genuinely irregular cells; any other cell must map to a known
            // ArgType and match the descriptor, so a new unrecognized or
            // mismatched spelling fails here.
            if (type_comparison_exceptions().count(qualified) == 0) {
                const std::optional<ArgType> doc_type =
                    doc_base_type(da->second.type);
                BOOST_TEST(doc_type.has_value(),
                           "section 3: '"
                               << qualified
                               << "' has an unrecognized Type base token '"
                               << da->second.type
                               << "' (add it to doc_base_type or, if genuinely "
                                  "irregular, to type_comparison_exceptions)"
                               << k_regen_hint);
                if (doc_type.has_value()) {
                    BOOST_TEST((*doc_type == spec.type),
                               "section 3: '"
                                   << qualified << "' Type mismatch (doc cell='"
                                   << da->second.type << "')" << k_regen_hint);
                }
            }

            if (is_enum_type(spec.type)
                && trim(da->second.type).rfind("enum", 0) == 0)
            {
                std::set<std::string> doc_enum;
                for (const std::string& v : backtick_tokens(da->second.type)) {
                    doc_enum.insert(v);
                }
                std::set<std::string> reg_enum(spec.enum_values.begin(),
                                               spec.enum_values.end());
                BOOST_TEST(doc_enum == reg_enum,
                           "section 3: enum members drift for '"
                               << qualified << "'" << k_regen_hint);
            }
        }
    }
#endif
}

// Scenario: every code of the 01.0 error enum appears in the section-1 error
//   table with the same HTTP status and CLI exit, and every documented row maps
//   to a real code - both directions pin the single all_error_infos() table.
//   When the conformance Python common module is present, its HTTP_STATUS /
//   CLI_EXIT maps are cross-checked against the same table.
// Boundaries: the `none` enumerator is not a wire failure and is not expected
//   in the table; the Python side is parsed leniently and skipped when absent.
// On failure first check: did a code's status/exit change in all_error_infos()
//   without updating the catalog row (and the Python maps)?
BOOST_AUTO_TEST_CASE(error_table_matches_code)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    // Registry -> doc: every code (except `none`) is a documented row.
    for (const auto& info : all_error_infos()) {
        if (info.code == CommandErrorCode::none) {
            continue;
        }
        const std::string wire(info.name);
        const auto row = doc.error_rows.find(wire);
        BOOST_REQUIRE_MESSAGE(row != doc.error_rows.end(),
                              "section 1 error table: code '"
                                  << wire << "' is missing" << k_regen_hint);
        BOOST_TEST(row->second.first == info.http_status,
                   "section 1 error table: HTTP mismatch for '"
                       << wire << "'" << k_regen_hint);
        BOOST_TEST(row->second.second == info.exit_code,
                   "section 1 error table: CLI exit mismatch for '"
                       << wire << "'" << k_regen_hint);
    }
    // Doc -> registry: every documented row is a real code.
    for (const auto& [wire, status_exit] : doc.error_rows) {
        const auto code = error_code_from_name(wire);
        BOOST_TEST(code.has_value(),
                   "section 1 error table: '" << wire
                                              << "' is not a CommandErrorCode"
                                              << k_regen_hint);
    }

    // Cross-check the conformance Python maps when that module exists. A code
    // in either dict that is not a known CommandErrorCode is a failure (the
    // Python harness must not carry a phantom code), not a silent skip.
    const auto py =
        read_file(source_dir + "/tests/conformance/py/conformance_common.py");
    if (py.has_value()) {
        const auto http = parse_python_int_dict(*py, "HTTP_STATUS");
        const auto exit = parse_python_int_dict(*py, "CLI_EXIT");
        for (const auto& [wire, status] : http) {
            const auto code = error_code_from_name(wire);
            BOOST_REQUIRE_MESSAGE(
                code.has_value(),
                "conformance_common.py HTTP_STATUS has unknown code '" << wire
                                                                       << "'");
            BOOST_TEST(error_info_for(*code).http_status
                           == static_cast<unsigned>(status),
                       "conformance_common.py HTTP_STATUS mismatch for '"
                           << wire << "'");
        }
        for (const auto& [wire, code_exit] : exit) {
            const auto code = error_code_from_name(wire);
            BOOST_REQUIRE_MESSAGE(
                code.has_value(),
                "conformance_common.py CLI_EXIT has unknown code '" << wire
                                                                    << "'");
            BOOST_TEST(
                error_info_for(*code).exit_code == code_exit,
                "conformance_common.py CLI_EXIT mismatch for '" << wire << "'");
        }
    }
#endif
}

// Scenario: the response field names documented by the section-1 JSON examples
//   equal the field names the 01.0 serializer emits across a success, an
//   accepted-job and an error envelope.
// Boundaries: the comparison is over the union of keys from all three sample
//   envelopes; result_json is an opaque payload and contributes no inner keys.
// On failure first check: did serialize_response add/rename an envelope field,
//   or did the section-1 JSON example drift from it?
BOOST_AUTO_TEST_CASE(envelope_response_fields_match_serializer)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    std::set<std::string> serialized;
    collect_json_keys(serialize_response(make_success("c-1", "{}")),
                      serialized);
    collect_json_keys(
        serialize_response(make_job_accepted("c-2", JobInfo {"j-1", {}})),
        serialized);
    collect_json_keys(
        serialize_response(make_failure(
            "c-3", make_error(CommandErrorCode::slug_conflict, "x"))),
        serialized);

    BOOST_TEST(doc.response_fields == serialized,
               "section 1: response envelope field names drift from "
               "serialize_response");
#endif
}

// Scenario: the request field names documented by the section-1 JSON example
//   are exactly those the 01.0 parser accepts: a canonical envelope built from
//   them parses, and an envelope with an undocumented top-level or options key
//   is rejected as invalid_envelope (the envelope is closed).
// Boundaries: dry_run/async are the only options fields; no request serializer
//   exists, so parse_request is the oracle.
// On failure first check: did the documented request shape drift from the
//   closed set parse_request enforces?
BOOST_AUTO_TEST_CASE(envelope_request_fields_match_parser)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    const std::set<std::string> expected = {
        "v", "id", "command", "args", "options", "dry_run", "async"};
    BOOST_TEST(doc.request_fields == expected,
               "section 1: request envelope field names drift from the "
               "documented v1 shape");

    // The documented shape parses.
    BOOST_TEST(parse_request(R"({"v":1,"id":"c-1","command":"link.get",)"
                             R"("args":{},"options":{"dry_run":false,)"
                             R"("async":false}})")
                   .ok());
    // An undocumented top-level field is rejected (closed envelope).
    BOOST_TEST(
        !parse_request(R"({"v":1,"command":"link.get","undocumented":true})")
             .ok());
    // An undocumented options field is rejected.
    BOOST_TEST(!parse_request(
                    R"({"v":1,"command":"link.get","options":{"nope":true}})")
                    .ok());
#endif
}

BOOST_AUTO_TEST_SUITE_END()

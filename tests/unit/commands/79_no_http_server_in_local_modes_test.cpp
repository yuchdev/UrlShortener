#define BOOST_TEST_MODULE NoHttpServerInLocalModesStructuralGuard
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#    include <process.h>
#else
#    include <unistd.h>
#endif

#include <boost/test/unit_test.hpp>

namespace fs = std::filesystem;

namespace
{

// ---------------------------------------------------------------------------
// Contract data (plan.md C5/C8/C11, threat IDs T-18/T-114, CLAUDE.md "Storage:
// ports and adapters" / "Redirect fast path is protected"). These tables ARE
// the invariant: the three local-mode entry points (`cmd`, `serve-stdio`,
// `tui`) must never construct `HttpServer`, bind/listen on a socket, build a
// Boost.Asio execution context, or touch `uri.txt` / `UriMapSingleton`; all
// business logic flows through the one `CommandRegistry` obtained from the
// single composition root. The structural scan here is a tripwire that fails
// loudly and early; the process-level proof lives in
// tests/conformance/py/32_no_listener_process_level.py.
//
// The scan is deliberately textual (it does not parse C++) and strips both
// `//` and `/* */` comments first, because every one of these files documents
// the invariant in prose ("must never ... touch `uri.txt`"); a naive grep
// would therefore false-positive on its own doc comment.
// ---------------------------------------------------------------------------

/// Include-target directory prefixes the local-mode entry points must never
/// reach - directly or transitively. The HTTP layer is the network/listener
/// surface (contract C5): no local mode may depend on it. Matched as a
/// substring of an include target so both `<...>` and `"..."` forms are caught.
constexpr std::array<std::string_view, 1> k_forbidden_include_dirs = {
    "url_shortener/http/",
};

/// System include targets that would pull in a Boost.Asio execution context /
/// acceptor into a local-mode translation unit (contract C5). Checked only as a
/// direct include of the three entry files (the transitive closure walk follows
/// only `url_shortener/...` includes, never Boost).
constexpr std::array<std::string_view, 2> k_forbidden_asio_includes = {
    "boost/asio",
    "asio/",
};

/// Identifiers that would betray a local mode constructing the network server,
/// an acceptor / Asio execution context, or the legacy URI store. A textual
/// presence of any of these (after comment stripping) in an entry file fails
/// `local_mode_entries_have_no_listener_or_uri_symbols`.
constexpr std::array<std::string_view, 8> k_forbidden_symbols = {
    "HttpServer",
    "io_context",
    "acceptor",
    "boost::asio",
    "UriMapSingleton",
    "uri_map_singleton",
    "uri.txt",
    "UriMap",
};

/// Ad-hoc registry / job-runner construction idioms. Only the one composition
/// root (`src/composition/command_runtime.cpp` /
/// `src/composition/job_runtime.cpp`) may create these; a local-mode entry file
/// that constructs its own fails `local_modes_use_one_composition_root` (C11).
/// The entry files legitimately *reference* the `CommandRegistry` type (they
/// execute through it), so the ban targets the construction expression, never
/// the bare type name.
constexpr std::array<std::string_view, 4> k_adhoc_registry_builders = {
    "make_shared<commands::CommandRegistry",
    "make_shared<commands::JobRunner",
    "new commands::CommandRegistry",
    "new commands::JobRunner",
};

/// The composition-root entry points: a local mode must call exactly one of
/// these to obtain the single shared runtime (C11). `serve-stdio` and `tui`
/// call `build_command_runtime` directly; `cmd` goes through the shared
/// `acquire_command_runtime` helper, which itself delegates to
/// `build_command_runtime`.
constexpr std::array<std::string_view, 2> k_composition_builders = {
    "build_command_runtime",
    "acquire_command_runtime",
};

/// The URI-store identifiers no file under `src/commands/**` or
/// `src/transport/**` may mention in code (contract C5). The milestone headers
/// document the invariant in prose, so the scan strips comments first.
constexpr std::array<std::string_view, 3> k_uri_store_symbols = {
    "UriMapSingleton",
    "uri_map_singleton",
    "uri.txt",
};

/// One `#include` line: its 1-based line number and the quoted/bracketed
/// target.
struct IncludeLine
{
    ///< 1-based line number within the scanned file.
    int line = 0;

    ///< The include target (text between `<...>` or `"..."`).
    std::string target;
};

/// @brief Returns the include target of one source line.
///
/// @param line A single source line.
/// @return The text between `<...>` or `"..."` for an `#include` directive, or
///         an empty string when @p line is not a well-formed `#include`.
std::string include_target(const std::string& line)
{
    std::size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line[i] != '#') {
        return {};
    }
    i = line.find_first_not_of(" \t", i + 1);
    const std::string_view keyword = "include";
    if (i == std::string::npos || line.compare(i, keyword.size(), keyword) != 0)
    {
        return {};
    }
    i = line.find_first_not_of(" \t", i + keyword.size());
    if (i == std::string::npos) {
        return {};
    }
    char close = '\0';
    if (line[i] == '<') {
        close = '>';
    }
    else if (line[i] == '"') {
        close = '"';
    }
    else {
        return {};
    }
    const std::size_t end = line.find(close, i + 1);
    if (end == std::string::npos) {
        return {};
    }
    return line.substr(i + 1, end - (i + 1));
}

/// @brief Reads a whole file into a string.
///
/// @param path Absolute path to the file to read.
/// @return The file's bytes, or an empty string when @p path cannot be opened
///         (a missing/unreadable file never produces a false guard failure).
std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// @brief Extracts every `#include` of a file with its line number.
///
/// @param path Absolute path to the source file to scan.
/// @return The `(line, target)` pairs of each `#include`; empty when the file
///         cannot be opened.
std::vector<IncludeLine> includes_of(const fs::path& path)
{
    std::vector<IncludeLine> includes;
    std::ifstream in(path);
    if (!in) {
        return includes;
    }
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::string target = include_target(line);
        if (!target.empty()) {
            includes.push_back(IncludeLine {number, target});
        }
    }
    return includes;
}

/// @brief Returns the source text with both `//` line comments and `/* ... */`
///        block comments removed, newlines preserved.
///
/// A small character state machine (it deliberately does not model string or
/// character literals, which the scanned files do not use to carry `//` or
/// `/*`): in code, `//` opens a line comment and `/*` a block comment; a line
/// comment runs to the next newline, a block comment to the closing `*/`.
/// Newlines are always preserved so the file's line structure stays intact.
/// Stripping block comments (not just `//`) keeps the symbol scan free of false
/// positives from a forbidden token that appears only inside a Doxygen block
/// comment - every entry file documents exactly these forbidden tokens.
///
/// @param text Full source text.
/// @return @p text with comment bodies removed and newlines preserved.
std::string strip_comments(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    enum class State
    {
        code,
        line_comment,
        block_comment
    } state = State::code;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';
        switch (state) {
            case State::code:
                if (c == '/' && next == '/') {
                    state = State::line_comment;
                    ++i;
                }
                else if (c == '/' && next == '*') {
                    state = State::block_comment;
                    ++i;
                }
                else {
                    out += c;
                }
                break;
            case State::line_comment:
                if (c == '\n') {
                    state = State::code;
                    out += c;
                }
                break;
            case State::block_comment:
                if (c == '*' && next == '/') {
                    state = State::code;
                    ++i;
                }
                else if (c == '\n') {
                    out += c;
                }
                break;
        }
    }
    return out;
}

/// @brief Whether an include target lies under a forbidden directory prefix.
///
/// @param target An include target.
/// @return `true` when @p target contains any entry of
///         @ref k_forbidden_include_dirs.
bool is_forbidden_include(const std::string& target)
{
    return std::any_of(k_forbidden_include_dirs.begin(),
                       k_forbidden_include_dirs.end(),
                       [&](std::string_view bad)
                       { return target.find(bad) != std::string::npos; });
}

/// @brief Whether an include target is a Boost.Asio / Asio header.
///
/// @param target An include target.
/// @return `true` when @p target contains any entry of
///         @ref k_forbidden_asio_includes.
bool is_asio_include(const std::string& target)
{
    return std::any_of(k_forbidden_asio_includes.begin(),
                       k_forbidden_asio_includes.end(),
                       [&](std::string_view bad)
                       { return target.find(bad) != std::string::npos; });
}

/// One hop that reaches a forbidden header, carrying the include chain that led
/// to it (seed filename -> ... -> forbidden target).
struct ClosureViolation
{
    ///< Human-readable include chain ending at the forbidden header.
    std::string chain;

    ///< The forbidden include target that terminated the walk.
    std::string target;
};

/// @brief Walks the transitive `url_shortener/...` include closure of @p seeds
///        and returns the first header under a forbidden directory.
///
/// Every `url_shortener/...` include is resolved to `@p include_root / target`
/// and followed with a visited set, so a forbidden header reachable only
/// indirectly - e.g. a composition/TUI header that later begins including
/// `http/...` - is still caught and its chain reported. Non-`url_shortener`
/// includes (std/Boost) are not followed; unreadable or missing headers are
/// skipped and never produce a false positive.
///
/// @param seeds Source files to start from (the three entry `.cpp` files).
/// @param include_root Directory under which `url_shortener/...` targets
///        resolve (the project `include/` tree, or a fixture tree in tests).
/// @return The first forbidden header with its include chain, or std::nullopt
///         when the whole reachable closure is clean.
std::optional<ClosureViolation> forbidden_in_include_closure(
    const std::vector<fs::path>& seeds, const fs::path& include_root)
{
    std::set<std::string> visited;
    std::vector<std::pair<fs::path, std::string>> stack;
    for (const auto& seed : seeds) {
        stack.emplace_back(seed, seed.filename().string());
    }
    while (!stack.empty()) {
        const fs::path path = stack.back().first;
        const std::string chain = stack.back().second;
        stack.pop_back();
        for (const auto& inc : includes_of(path)) {
            if (inc.target.rfind("url_shortener/", 0) != 0) {
                continue;
            }
            const std::string next_chain = chain + " -> " + inc.target;
            if (is_forbidden_include(inc.target)) {
                return ClosureViolation {next_chain, inc.target};
            }
            if (!visited.insert(inc.target).second) {
                continue;
            }
            const fs::path header = include_root / inc.target;
            std::error_code ec;
            if (fs::exists(header, ec)) {
                stack.emplace_back(header, next_chain);
            }
        }
    }
    return std::nullopt;
}

/// @brief The project source root, or an empty path when `SOURCE_DIR` is not
///        defined (e.g. a direct, non-CTest run).
fs::path source_root()
{
#ifdef SOURCE_DIR
    return fs::path(SOURCE_DIR);
#else
    return {};
#endif
}

/// @brief Absolute path to the `cmd` one-shot dispatch translation unit.
fs::path cmd_dispatch_cpp()
{
    return source_root() / "src" / "cli" / "cmd_dispatch.cpp";
}

/// @brief Absolute path to the `serve-stdio` dispatch translation unit.
fs::path serve_stdio_cpp()
{
    return source_root() / "src" / "cli" / "serve_stdio_dispatch.cpp";
}

/// @brief Absolute path to the `tui` entry translation unit.
fs::path tui_entry_cpp()
{
    return source_root() / "src" / "tui" / "tui_entry.cpp";
}

/// @brief The three local-mode entry translation units the guard scans.
///
/// The scan is restricted to these three explicitly named, stable files (never
/// a recursive directory walk) so concurrent edits to unrelated sources can
/// never make this guard flaky.
std::array<fs::path, 3> local_mode_entries()
{
    return {cmd_dispatch_cpp(), serve_stdio_cpp(), tui_entry_cpp()};
}

/// @brief The current process id, used to make self-test temp dirs unique.
unsigned long process_token()
{
#if defined(_WIN32)
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

/// @brief RAII unique temp directory for the negative self-tests.
///
/// The directory name combines a caller tag, the process id and two random
/// draws, so concurrent runs (parallel CTest, multiple agents) never collide;
/// the destructor removes the whole tree even when an assertion throws out of
/// the test body.
class TempDir
{
public:
    /// @brief Creates a fresh unique temp directory.
    /// @param tag Short human tag embedded in the directory name.
    explicit TempDir(const std::string& tag)
    {
        std::random_device rd;
        const std::string unique = tag + "_" + std::to_string(process_token())
            + "_" + std::to_string(rd()) + "_" + std::to_string(rd());
        m_path_ = fs::temp_directory_path()
            / ("url_shortener_local_mode_guard_" + unique);
        std::error_code ec;
        fs::remove_all(m_path_, ec);
        fs::create_directories(m_path_, ec);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(m_path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /// @brief The absolute path to the created temp directory.
    const fs::path& path() const { return m_path_; }

private:
    ///< Absolute path to the unique temp directory owned by this guard.
    fs::path m_path_;
};

/// @brief Whether a comment-stripped source text references any composition
///        builder of @ref k_composition_builders.
bool references_composition_builder(const std::string& code)
{
    return std::any_of(k_composition_builders.begin(),
                       k_composition_builders.end(),
                       [&](std::string_view builder)
                       { return code.find(builder) != std::string::npos; });
}

}  // namespace

// Type: unit (include-hygiene guard; plan.md C5, threat T-18: a local-mode
//   entry file growing a dependency on the HTTP/listener layer or a Boost.Asio
//   execution context via a direct include).
// Scenario: none of src/cli/cmd_dispatch.cpp, src/cli/serve_stdio_dispatch.cpp
//   or src/tui/tui_entry.cpp directly includes a header under
//   url_shortener/http/ or any boost/asio header.
// Boundaries: adding `#include <url_shortener/http/http_server.hpp>` or
//   `#include <boost/asio.hpp>` to any of the three fails with the exact file
//   and line. The three files are named explicitly, so a concurrent edit to an
//   unrelated source cannot make this flaky.
// On failure first check: did a local mode start reaching for the network
//   server or an Asio context? Local modes resolve everything through the one
//   CommandRegistry from composition::build_command_runtime; the listener lives
//   only in server mode (src/main.cpp).
BOOST_AUTO_TEST_CASE(local_mode_entries_have_no_http_or_asio_includes)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    bool scanned_any = false;
    for (const auto& file : local_mode_entries()) {
        BOOST_REQUIRE_MESSAGE(fs::exists(file),
                              "entry source not found: " << file.string());
        for (const auto& inc : includes_of(file)) {
            scanned_any = true;
            const std::string where =
                file.filename().string() + ":" + std::to_string(inc.line);
            BOOST_TEST(!is_forbidden_include(inc.target),
                       "forbidden HTTP-layer include at "
                           << where << " -> " << inc.target
                           << " (C5: local modes must not depend on the HTTP "
                              "listener surface)");
            BOOST_TEST(!is_asio_include(inc.target),
                       "forbidden Boost.Asio include at "
                           << where << " -> " << inc.target
                           << " (C5: local modes construct no execution "
                              "context / acceptor)");
        }
    }
    BOOST_TEST(scanned_any, "no includes scanned; entry sources missing?");
}

// Type: unit (transitive include-closure guard; plan.md C5, threat T-18/T-114:
//   the flat include check alone cannot see a forbidden header pulled in
//   indirectly through a composition/TUI header).
// Scenario: walking every url_shortener include reachable from the three entry
//   .cpp files through include/url_shortener/... (with a visited set) reaches
//   no header under url_shortener/http/.
// Boundaries: if a future edit makes a reachable header such as a composition
//   or tui header include url_shortener/http/..., this fails and prints the
//   full include chain, even though the entry .cpp itself stayed clean. The
//   walk follows only url_shortener includes; std/Boost are ignored.
// On failure first check: read the reported chain; the last header before the
//   http/ target is the one that gained the HTTP dependency - break that edge,
//   do not add http/ back into a local-mode path.
BOOST_AUTO_TEST_CASE(local_mode_entry_closure_has_no_http_headers)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    const auto violation = forbidden_in_include_closure(
        {cmd_dispatch_cpp(), serve_stdio_cpp(), tui_entry_cpp()},
        root / "include");
    BOOST_TEST(
        !violation.has_value(),
        "HTTP-layer header reachable from a local-mode entry point via "
            << (violation.has_value() ? violation->chain : std::string {}));
}

// Type: unit (textual symbol guard; plan.md C5, threats T-18/T-114).
// Scenario: the three entry sources contain none of the listener / Asio / URI-
//   store identifiers (HttpServer, io_context, acceptor, boost::asio,
//   UriMapSingleton, uri_map_singleton, uri.txt, UriMap), ignoring both `//`
//   and `/* */` comments - every file documents exactly these tokens in prose,
//   so stripping comments is load-bearing, not cosmetic.
// Boundaries: a stray `HttpServer server(...)` or `UriMapSingleton::instance()`
//   in an entry file fails here even if its header slipped past the include
//   checks.
// On failure first check: did a local mode start the server, build an acceptor,
//   or open the legacy uri.txt store? None of that belongs off the server path.
BOOST_AUTO_TEST_CASE(local_mode_entries_have_no_listener_or_uri_symbols)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    for (const auto& file : local_mode_entries()) {
        const std::string code = strip_comments(read_file(file));
        BOOST_REQUIRE_MESSAGE(
            !code.empty(), "entry source empty/unreadable: " << file.string());
        for (const std::string_view symbol : k_forbidden_symbols) {
            BOOST_TEST(
                code.find(symbol) == std::string::npos,
                "forbidden listener/URI-store symbol '"
                    << symbol << "' in " << file.filename().string()
                    << " (C5: local modes bind no socket and never touch "
                       "the legacy URI store)");
        }
    }
}

// Type: unit (composition-root guard; plan.md C11, threat T-114).
// Scenario: each entry source calls exactly one composition builder
//   (build_command_runtime for serve-stdio/tui, acquire_command_runtime for
//   cmd) and constructs no ad-hoc CommandRegistry/JobRunner (the construction
//   idioms make_shared<commands::CommandRegistry|JobRunner> / new
//   commands::CommandRegistry|JobRunner), ignoring comments.
// Boundaries: an entry file that builds its own registry (a split store, T-114)
//   or that stops routing through the composition root fails here. The ban
//   targets construction, not the bare CommandRegistry type name, which the
//   dispatchers legitimately reference to execute.
// On failure first check: did a local mode construct a registry/runner instead
//   of obtaining the one shared runtime? Composition belongs to the single
//   composition root over the active backend, never a dispatcher.
BOOST_AUTO_TEST_CASE(local_modes_use_one_composition_root)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    for (const auto& file : local_mode_entries()) {
        const std::string code = strip_comments(read_file(file));
        BOOST_REQUIRE_MESSAGE(
            !code.empty(), "entry source empty/unreadable: " << file.string());
        BOOST_TEST(references_composition_builder(code),
                   "entry source "
                       << file.filename().string()
                       << " does not call a composition builder "
                          "(build_command_runtime / acquire_command_runtime); "
                          "C11 requires one composition root");
        for (const std::string_view builder : k_adhoc_registry_builders) {
            BOOST_TEST(code.find(builder) == std::string::npos,
                       "ad-hoc registry/runner construction '"
                           << builder << "' in " << file.filename().string()
                           << " (C11: only the composition root builds the "
                              "registry/job runner)");
        }
    }
}

// Type: unit (URI-store guard; plan.md C5, threat T-114).
// Scenario: no translation unit under src/commands/ or src/transport/ mentions
//   UriMapSingleton / uri_map_singleton / uri.txt in code (comments stripped).
// Boundaries: a command or transport file that opens or references the legacy
//   URI store fails here with the offending file. Comments are stripped first
//   because several of these headers document the "never touches uri.txt"
//   invariant in prose.
// On failure first check: which command/transport file started touching the URI
//   store? The command and transport layers are store-agnostic; persistence is
//   reached only through IMetadataRepository behind the composition root.
BOOST_AUTO_TEST_CASE(src_commands_do_not_touch_uri_store)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    const std::array<fs::path, 4> roots = {
        root / "src" / "commands",
        root / "src" / "transport",
        root / "include" / "url_shortener" / "commands",
        root / "include" / "url_shortener" / "transport",
    };

    bool scanned_any = false;
    for (const auto& dir : roots) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            continue;
        }
        fs::recursive_directory_iterator it(
            dir, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const auto& entry = *it;
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const auto ext = entry.path().extension();
            if (ext != ".cpp" && ext != ".hpp") {
                continue;
            }
            scanned_any = true;
            const std::string code = strip_comments(read_file(entry.path()));
            for (const std::string_view symbol : k_uri_store_symbols) {
                BOOST_TEST(code.find(symbol) == std::string::npos,
                           "URI-store symbol '"
                               << symbol << "' in "
                               << entry.path().filename().string()
                               << " (C5: the command/transport layer never "
                                  "touches the legacy URI store)");
            }
        }
    }
    BOOST_TEST(scanned_any, "no command/transport sources scanned?");
}

// Type: unit (negative self-test of the direct include and symbol guards).
// Scenario: a fixture carrying an http/ include, a boost/asio include, a
//   forbidden code symbol, and the same tokens inside both a `//` and a
//   `/* */` comment is reported forbidden by includes_of/is_forbidden_include,
//   is_asio_include and the symbol scan, while the two commented tokens are
//   correctly ignored.
// Boundaries: proves the direct guards would actually catch a real violation
//   (file and line reported) instead of passing vacuously, and that both
//   comment forms are stripped; if include_target or the comment stripper
//   regressed, this case goes red.
// On failure first check: did include parsing or comment stripping stop
//   working? Fix the helper; do not weaken the real guards above.
BOOST_AUTO_TEST_CASE(guard_flags_planted_violations)
{
    const TempDir dir("selftest");
    const fs::path tainted = dir.path() / "tainted.cpp";
    {
        std::ofstream out(tainted);
        out << "#include <string>\n";  // 1
        out << "#include <url_shortener/http/http_server.hpp>\n";  // 2
        out << "#include <boost/asio.hpp>\n";  // 3
        out << "// HttpServer here is only a line comment\n";  // 4
        out << "/* UriMapSingleton only in a block comment */\n";  // 5
        out << "void f() { io_context ctx; }\n";  // 6
    }

    const auto includes = includes_of(tainted);
    bool found_http = false;
    bool found_asio = false;
    for (const auto& inc : includes) {
        if (is_forbidden_include(inc.target)) {
            found_http = true;
            BOOST_TEST(inc.line == 2);
            BOOST_TEST(inc.target == "url_shortener/http/http_server.hpp");
        }
        if (is_asio_include(inc.target)) {
            found_asio = true;
            BOOST_TEST(inc.line == 3);
        }
    }
    BOOST_TEST(found_http);
    BOOST_TEST(found_asio);

    // Symbol scan: io_context (code) is caught; HttpServer (line comment) and
    // UriMapSingleton (block comment) are both stripped first.
    const std::string stripped = strip_comments(read_file(tainted));
    BOOST_TEST(stripped.find("io_context") != std::string::npos);
    BOOST_TEST(stripped.find("HttpServer") == std::string::npos);
    BOOST_TEST(stripped.find("UriMapSingleton") == std::string::npos);
}

// Type: unit (negative self-test of the transitive include-closure guard).
// Scenario: a fixture seed .cpp includes a composition-shaped header
//   composition/hop.hpp which itself includes url_shortener/http/evil.hpp; the
//   closure walk reports the forbidden header with the full chain.
// Boundaries: proves forbidden_in_include_closure follows indirect edges (not
//   just the seed's direct includes) and surfaces the chain; a clean sibling
//   tree yields no violation.
// On failure first check: did the closure walk stop following url_shortener
//   includes or stop resolving them under the include root?
BOOST_AUTO_TEST_CASE(guard_flags_transitive_http_include)
{
    const TempDir dir("closure");
    const fs::path include_root = dir.path() / "include";
    const fs::path seed = dir.path() / "seed.cpp";
    fs::create_directories(include_root / "url_shortener" / "composition");
    fs::create_directories(include_root / "url_shortener" / "http");
    {
        std::ofstream out(seed);
        out << "#include <url_shortener/composition/hop.hpp>\n";
    }
    {
        std::ofstream out(include_root / "url_shortener" / "composition"
                          / "hop.hpp");
        out << "#include <url_shortener/http/evil.hpp>\n";
    }
    {
        std::ofstream out(include_root / "url_shortener" / "http" / "evil.hpp");
        out << "#pragma once\n";
    }

    const auto violation = forbidden_in_include_closure({seed}, include_root);
    BOOST_REQUIRE(violation.has_value());
    BOOST_TEST(violation->target == "url_shortener/http/evil.hpp");
    BOOST_TEST(violation->chain
               == "seed.cpp -> url_shortener/composition/hop.hpp -> "
                  "url_shortener/http/evil.hpp");

    // A clean tree (drop the forbidden leaf include) yields no violation.
    {
        std::ofstream out(
            include_root / "url_shortener" / "composition" / "hop.hpp",
            std::ios::trunc);
        out << "#pragma once\n";
    }
    BOOST_TEST(!forbidden_in_include_closure({seed}, include_root).has_value());
}

// Type: unit (negative self-test of the composition-root and URI-store guards).
// Scenario: a fixture file that both constructs an ad-hoc registry
//   (make_shared<commands::CommandRegistry>()) and mentions uri.txt in code is
//   flagged by the construction ban and the URI-store symbol set, while a file
//   that calls build_command_runtime and keeps uri.txt only in a comment is
//   clean.
// Boundaries: proves the C11 construction ban and the URI-store scan are not
//   vacuous - a real ad-hoc registry and a real uri.txt reference are both
//   detected, and comment-only prose is ignored.
// On failure first check: did the construction-idiom list drift from the real
//   factory call, or did comment stripping regress?
BOOST_AUTO_TEST_CASE(guard_flags_adhoc_registry_and_uri_store)
{
    const TempDir dir("adhoc");
    const fs::path bad = dir.path() / "bad.cpp";
    const fs::path good = dir.path() / "good.cpp";
    {
        std::ofstream out(bad);
        out << "auto r = std::make_shared<commands::CommandRegistry>();\n";
        out << "std::ifstream in(\"uri.txt\");\n";
    }
    {
        std::ofstream out(good);
        out << "auto rt = composition::build_command_runtime(cfg, be, opt);\n";
        out << "// never opens uri.txt / UriMapSingleton\n";
    }

    const std::string bad_code = strip_comments(read_file(bad));
    bool adhoc_flagged = false;
    for (const std::string_view builder : k_adhoc_registry_builders) {
        if (bad_code.find(builder) != std::string::npos) {
            adhoc_flagged = true;
        }
    }
    BOOST_TEST(adhoc_flagged);
    BOOST_TEST(bad_code.find("uri.txt") != std::string::npos);
    BOOST_TEST(!references_composition_builder(bad_code));

    const std::string good_code = strip_comments(read_file(good));
    BOOST_TEST(references_composition_builder(good_code));
    for (const std::string_view symbol : k_uri_store_symbols) {
        BOOST_TEST(good_code.find(symbol) == std::string::npos);
    }
    for (const std::string_view builder : k_adhoc_registry_builders) {
        BOOST_TEST(good_code.find(builder) == std::string::npos);
    }
}

#define BOOST_TEST_MODULE RedirectIsolationStructuralGuard
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
#include <url_shortener/http/route_registry.hpp>

namespace fs = std::filesystem;

using url_shortener::http::registeredRoutes;
using url_shortener::http::RouteDescriptor;

namespace
{

// ---------------------------------------------------------------------------
// Allowlist and forbidden-token data (plan.md C10, CLAUDE.md "Redirect fast
// path is protected"). These tables ARE the contract: tightening or loosening
// the redirect layer's dependencies must be a visible, reviewed edit here.
//
// The allowlist below was captured from the redirect sources as left by Task
// 02.0 subtask 08 (redirect handlers re-pointed at `LinkBackend`). Only Task
// 02.0 is permitted to change these includes, and only together with an
// explicit justification in this table (subtask 31 spec, implementation
// step 3).
// ---------------------------------------------------------------------------

/// Every `url_shortener/...` include target the redirect translation unit and
/// its header are allowed to name. Any `url_shortener/...` include outside this
/// set is a layering violation and fails
/// `redirect_sources_include_allowlist_only`.
constexpr std::array<std::string_view, 8> k_allowed_url_shortener_includes = {
    "url_shortener/analytics/click_event_queue.hpp",
    "url_shortener/app/app_error.hpp",
    "url_shortener/app/link_backend.hpp",
    "url_shortener/core/utils.hpp",
    "url_shortener/http/handler_types.hpp",
    "url_shortener/http/handlers/redirect_handlers.hpp",
    "url_shortener/http/request_handlers.hpp",
    "url_shortener/storage/memory/in_memory_click_event_repository.hpp",
};

/// Directory prefixes the redirect layer must never reach into: the command
/// model, the TUI, transport adapters, the job runtime, and the composition
/// root. Matched as a substring of an include target so both `<...>` and
/// `"..."` forms are caught (C10; redirect code must not depend on the command
/// layer, TUI, job runtime, or transport adapters).
constexpr std::array<std::string_view, 5> k_forbidden_include_dirs = {
    "url_shortener/commands/",
    "url_shortener/tui/",
    "url_shortener/transport/",
    "url_shortener/jobs",
    "url_shortener/composition/",
};

/// Identifiers that would betray the redirect fast path reaching into the
/// command/job/transport machinery. A textual presence of any of these in the
/// redirect sources fails `redirect_sources_have_no_command_symbols`; the scan
/// is deliberately textual (it does not parse C++) and ignores `//` comments.
constexpr std::array<std::string_view, 7> k_forbidden_symbols = {
    "CommandRegistry",
    "CommandRequest",
    "ICommandClient",
    "JobRunner",
    "ITransportAdapter",
    "ArgSpec",
    "execute(",
};

/// Composition-root / job-runtime builder functions the redirect handlers must
/// never call (Task 03.0 subtask 18 `build_command_runtime`, Task 05.0 subtask
/// 20 `process_job_runner`). Presence fails
/// `redirect_handlers_do_not_construct_registry`.
constexpr std::array<std::string_view, 2> k_registry_builders = {
    "build_command_runtime",
    "process_job_runner",
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
std::string readFile(const fs::path& path)
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
std::vector<IncludeLine> includesOf(const fs::path& path)
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

/// @brief Returns the source text with both `//` line comments and
///        `/* ... */` block comments removed.
///
/// A small character state machine (it deliberately does not model string or
/// character literals, which the scanned files do not use to carry `//` or
/// `/*`): in code, `//` opens a line comment and `/*` a block comment; a line
/// comment runs to the next newline, a block comment to the closing `*/`.
/// Newlines are always preserved so the file's line structure and any
/// downstream line-based reasoning stay intact. Stripping block comments (not
/// just `//`) keeps the symbol scan free of false positives from a forbidden
/// token that appears only inside a Doxygen block comment.
///
/// @param text Full source text.
/// @return @p text with comment bodies removed and newlines preserved.
std::string strip_line_comments(const std::string& text)
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

/// @brief Whether a `url_shortener/...` include is on the allowlist.
///
/// @param target An include target already known to start with
/// `url_shortener/`.
/// @return `true` when @p target is one of
///         @ref k_allowed_url_shortener_includes.
bool is_allowed_url_shortener(const std::string& target)
{
    return std::any_of(k_allowed_url_shortener_includes.begin(),
                       k_allowed_url_shortener_includes.end(),
                       [&](std::string_view ok) { return target == ok; });
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
///        and returns the first header that lies under a forbidden directory.
///
/// Every `url_shortener/...` include is resolved to `@p include_root / target`
/// and followed with a visited set, so a forbidden header reachable only
/// indirectly - e.g. an allowlisted `app/link_backend.hpp` that later begins
/// including `commands/...` - is still caught and its chain reported.
/// Non-`url_shortener` includes (std/Boost) are not followed; unreadable or
/// missing headers are skipped and never produce a false positive.
///
/// @param seeds Source files to start from (the redirect `.cpp` and `.hpp`).
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
        for (const auto& inc : includesOf(path)) {
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

/// @brief Absolute path to the redirect translation unit.
fs::path redirect_cpp()
{
    return source_root() / "src" / "http" / "handlers"
        / "redirect_handlers.cpp";
}

/// @brief Absolute path to the redirect handlers header.
fs::path redirect_hpp()
{
    return source_root() / "include" / "url_shortener" / "http" / "handlers"
        / "redirect_handlers.hpp";
}

/// @brief The two redirect source files the guard scans.
///
/// The scan is restricted to these two explicitly named, stable files (never a
/// recursive walk of `src/http/handlers/`) so concurrent edits to unrelated
/// handlers can never make this guard flaky.
std::array<fs::path, 2> redirect_sources()
{
    return {redirect_cpp(), redirect_hpp()};
}

/// @brief Locates a route by method and path pattern in `registeredRoutes()`.
///
/// @param method HTTP method (e.g. "GET").
/// @param path Path pattern (e.g. "/{slug}").
/// @return Pointer to the descriptor, or `nullptr` when absent.
const RouteDescriptor* find_route(const std::string& method,
                                  const std::string& path)
{
    const auto& routes = registeredRoutes();
    const auto it = std::find_if(
        routes.begin(),
        routes.end(),
        [&](const RouteDescriptor& route)
        { return route.method == method && route.path_pattern == path; });
    return it == routes.end() ? nullptr : &*it;
}

/// @brief Index of a route in `registeredRoutes()`, or -1 when absent.
int route_index(const std::string& method, const std::string& path)
{
    const auto& routes = registeredRoutes();
    for (std::size_t i = 0; i < routes.size(); ++i) {
        if (routes[i].method == method && routes[i].path_pattern == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// The two disjoint buckets of "redirect-ish" routes: those semantically tagged
/// as redirects, and those that merely share the `redirect_` metric label (the
/// URI-store fallback) without being redirect surfaces.
struct RedirectPartition
{
    ///< Routes carrying the "Redirects" tag, formatted "METHOD PATH -> label".
    std::set<std::string> tagged;

    ///< Routes whose `route_label` starts with "redirect_" but are NOT tagged
    ///< "Redirects" - the `/{path}` URI-store fallback shares the metric label.
    std::set<std::string> labeled_only;
};

/// @brief Partitions routes into redirect-tagged and redirect-labeled-only
///        sets.
///
/// The `/{path}` URI-store fallback routes carry the metric label
/// `redirect_root` yet are tagged "Fallback", not "Redirects"; keeping the two
/// buckets separate lets the pin assert both the genuine redirect surface and
/// the known label-sharers exactly. The same logic serves the live registry
/// and the negative self-test's synthetic routes.
///
/// @param routes Route descriptors to classify.
/// @return A @ref RedirectPartition.
RedirectPartition partition_redirects(
    const std::vector<RouteDescriptor>& routes)
{
    RedirectPartition parts;
    for (const auto& route : routes) {
        const std::string key = route.method + " " + route.path_pattern + " -> "
            + route.route_label;
        const bool tagged_redirect =
            std::find(route.tags.begin(), route.tags.end(), "Redirects")
            != route.tags.end();
        const bool redirect_labeled =
            route.route_label.rfind("redirect_", 0) == 0;
        if (tagged_redirect) {
            parts.tagged.insert(key);
        }
        else if (redirect_labeled) {
            parts.labeled_only.insert(key);
        }
    }
    return parts;
}

/// @brief Joins a set of strings into a single diagnostic line.
///
/// @param items The strings to join.
/// @return The items separated by "; ", or an empty string when @p items is
///         empty.
std::string join_set(const std::set<std::string>& items)
{
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) {
            out += "; ";
        }
        out += item;
    }
    return out;
}

/// @brief Extracts a top-level Markdown section by heading prefix.
///
/// @param doc Full document text.
/// @param heading_prefix The section heading start (e.g. "## 5.").
/// @return The text from @p heading_prefix up to (not including) the next
///         top-level "## " heading, or an empty string when @p heading_prefix
///         is absent.
std::string markdown_section(const std::string& doc,
                             const std::string& heading_prefix)
{
    const std::size_t start = doc.find(heading_prefix);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t next = doc.find("\n## ", start + heading_prefix.size());
    return doc.substr(
        start, next == std::string::npos ? std::string::npos : next - start);
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
            / ("url_shortener_redirect_guard_" + unique);
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

}  // namespace

// Type: unit (layering / include-hygiene guard; plan.md C10, CLAUDE.md
//   "Redirect fast path is protected", threat: redirect code growing a
//   dependency on the command/TUI/job/transport layers).
// Scenario: every `#include` of src/http/handlers/redirect_handlers.cpp and its
//   header is either a std/Boost header or one of the eight allowlisted
//   url_shortener includes; no include reaches into commands/, tui/,
//   transport/, jobs, or composition/.
// Boundaries: adding `#include <url_shortener/commands/command_registry.hpp>`
//   to the redirect .cpp fails this with the exact file and line; the allowlist
//   can only change through a reviewed edit to this file.
// On failure first check: did Task 02.0 intentionally re-point the redirect
//   storage access? If so update k_allowed_url_shortener_includes with a
//   justification; otherwise the new include is the layering violation to drop.
BOOST_AUTO_TEST_CASE(redirect_sources_include_allowlist_only)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    bool scanned_any = false;
    for (const auto& file : redirect_sources()) {
        BOOST_REQUIRE_MESSAGE(fs::exists(file),
                              "redirect source not found: " << file.string());
        const auto includes = includesOf(file);
        for (const auto& inc : includes) {
            scanned_any = true;
            const std::string where =
                file.filename().string() + ":" + std::to_string(inc.line);

            BOOST_TEST(!is_forbidden_include(inc.target),
                       "forbidden redirect dependency at "
                           << where << " -> " << inc.target
                           << " (C10: redirect code must not reach the "
                              "command/tui/transport/jobs/composition layers)");

            if (inc.target.rfind("url_shortener/", 0) == 0) {
                BOOST_TEST(is_allowed_url_shortener(inc.target),
                           "redirect include not on allowlist at "
                               << where << " -> " << inc.target
                               << " (edit k_allowed_url_shortener_includes, "
                                  "reviewed, only for Task 02.0)");
            }
        }
    }
    BOOST_TEST(scanned_any, "no includes scanned; redirect sources missing?");
}

// Type: unit (transitive include-closure guard; plan.md C10, security review
//   MEDIUM: the flat allowlist alone cannot see a forbidden header pulled in
//   indirectly through an allowlisted header).
// Scenario: walking every url_shortener include reachable from the redirect
//   .cpp/.hpp through include/url_shortener/... (with a visited set) reaches no
//   header under commands/, tui/, transport/, jobs, or composition/.
// Boundaries: if a future edit makes an allowlisted header such as
//   app/link_backend.hpp include url_shortener/commands/..., this fails and
//   prints the full include chain, even though the redirect .cpp itself stayed
//   clean. The walk follows only url_shortener includes; std/Boost are ignored.
// On failure first check: read the reported chain; the last allowlisted header
//   in it is the one that gained a command/tui/transport/jobs/composition
//   dependency - break that edge, do not widen the redirect allowlist.
BOOST_AUTO_TEST_CASE(redirect_include_closure_has_no_forbidden_headers)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    const auto violation = forbidden_in_include_closure(
        {redirect_cpp(), redirect_hpp()}, root / "include");
    BOOST_TEST(
        !violation.has_value(),
        "forbidden header reachable from the redirect sources via "
            << (violation.has_value() ? violation->chain : std::string {}));
}

// Type: unit (textual symbol guard; plan.md C10).
// Scenario: the redirect sources contain none of the command/job/transport
//   identifiers (CommandRegistry, CommandRequest, ICommandClient, JobRunner,
//   ITransportAdapter, ArgSpec, execute(), ignoring both `//` and `/* */`
//   comments so prose in a Doxygen block cannot trip the scan.
// Boundaries: a stray `registry.execute(` or `CommandRequest req;` in redirect
//   code fails here even if its header slipped past the include allowlist.
// On failure first check: did redirect logic start building or invoking a
//   command/job object? Move that logic into LinkService/LinkBackend; the
//   redirect path stays cache lookup + repository fallback + response.
BOOST_AUTO_TEST_CASE(redirect_sources_have_no_command_symbols)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    for (const auto& file : redirect_sources()) {
        const std::string code = strip_line_comments(readFile(file));
        BOOST_REQUIRE_MESSAGE(
            !code.empty(),
            "redirect source empty/unreadable: " << file.string());
        for (const std::string_view symbol : k_forbidden_symbols) {
            BOOST_TEST(code.find(symbol) == std::string::npos,
                       "forbidden command-layer symbol '"
                           << symbol << "' in " << file.filename().string()
                           << " (C10: redirect fast path must not touch the "
                              "command/job/transport machinery)");
        }
    }
}

// Type: unit (route-table guard; plan.md C10, closes the verifier's section-5
//   gap).
// Scenario: the redirect routes carry their exact redirect metadata
//   (GET /{slug} -> redirect_root, GET /r/{slug} -> redirect_prefixed, tag
//   "Redirects", never tag "Commands"), AND neither redirect route appears in
//   section 5 "Static route -> command table" of docs/commands/README.md.
// Boundaries: re-labeling a redirect route (e.g. to api_commands), tagging it
//   "Commands", or adding a row for it to the command route table fails this.
//   The RouteDescriptor has no dedicated command-mapping field in this
//   milestone, so the exact route_label/tag plus the catalog table are the
//   available command-association signals.
// On failure first check: did a route-table or catalog edit reclassify a
//   redirect as a command surface? Redirects belong to the fast path, not the
//   command plane.
BOOST_AUTO_TEST_CASE(redirect_routes_not_in_command_route_table)
{
    const std::array<std::array<std::string, 3>, 2> redirects = {{
        {"GET", "/{slug}", "redirect_root"},
        {"GET", "/r/{slug}", "redirect_prefixed"},
    }};

    for (const auto& entry : redirects) {
        const std::string& method = entry[0];
        const std::string& path = entry[1];
        const std::string& expected_label = entry[2];
        const RouteDescriptor* route = find_route(method, path);
        BOOST_REQUIRE_MESSAGE(
            route != nullptr,
            "redirect route missing from registry: " << method << ' ' << path);

        BOOST_TEST(route->route_label == expected_label,
                   "redirect route " << method << ' ' << path << " has label '"
                                     << route->route_label << "', expected '"
                                     << expected_label << '\'');

        const bool tagged_command =
            std::find(route->tags.begin(), route->tags.end(), "Commands")
            != route->tags.end();
        BOOST_TEST(!tagged_command,
                   "redirect route " << method << ' ' << path
                                     << " carries the Commands tag");

        const bool tagged_redirect =
            std::find(route->tags.begin(), route->tags.end(), "Redirects")
            != route->tags.end();
        BOOST_TEST(tagged_redirect,
                   "redirect route " << method << ' ' << path
                                     << " lost its Redirects tag");
    }

    // Section 5 of docs/commands/README.md is the static route -> command
    // table (the REST resource projection). No redirect route may appear there.
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping catalog scan");
        return;
    }
    const fs::path catalog = root / "docs" / "commands" / "README.md";
    if (!fs::exists(catalog)) {
        BOOST_TEST_MESSAGE("docs/commands/README.md absent; skipping scan");
        return;
    }
    const std::string section = markdown_section(readFile(catalog), "## 5.");
    BOOST_REQUIRE_MESSAGE(
        !section.empty(),
        "command catalog section 5 (static route -> command table) not found");
    // A bare "/{slug}" substring also occurs in the /api/v1/links/{slug} rows,
    // so match the method-qualified redirect cells instead.
    BOOST_TEST(section.find("GET /{slug}") == std::string::npos,
               "redirect route GET /{slug} appears in the command route table "
               "(docs/commands/README.md section 5)");
    BOOST_TEST(section.find("/r/{slug}") == std::string::npos,
               "redirect route /r/{slug} appears in the command route table "
               "(docs/commands/README.md section 5)");
}

// Type: unit (router priority guard; plan.md C10, mirrors the ordering checked
//   by tests/unit/http/08_router_registry_consistency.cpp).
// Scenario: in registeredRoutes() both redirect routes appear after every
//   /api/v1/* family entry and before the generic /{path} URI-store fallback.
// Boundaries: moving /{slug} ahead of the /api/v1/links family, or after the
//   /{path} fallback, fails this: redirect priority is load-bearing for the
//   fast path and for not shadowing the management plane.
// On failure first check: did RouterBuilder/registry ordering change? Redirects
//   must stay after /api/v1/* and before the URI-store fallback.
BOOST_AUTO_TEST_CASE(redirect_route_priority_unchanged)
{
    const auto& routes = registeredRoutes();

    const int root_redirect = route_index("GET", "/{slug}");
    const int prefixed_redirect = route_index("GET", "/r/{slug}");
    const int fallback = route_index("GET", "/{path}");
    BOOST_REQUIRE(root_redirect >= 0);
    BOOST_REQUIRE(prefixed_redirect >= 0);
    BOOST_REQUIRE(fallback >= 0);

    // Last /api/v1/* family entry in registration order.
    int last_api = -1;
    for (std::size_t i = 0; i < routes.size(); ++i) {
        if (routes[i].path_pattern.rfind("/api/v1/", 0) == 0) {
            last_api = static_cast<int>(i);
        }
    }
    BOOST_REQUIRE(last_api >= 0);

    BOOST_TEST(prefixed_redirect > last_api,
               "GET /r/{slug} must register after the /api/v1/* families");
    BOOST_TEST(root_redirect > last_api,
               "GET /{slug} must register after the /api/v1/* families");
    BOOST_TEST(prefixed_redirect < fallback,
               "GET /r/{slug} must register before the /{path} fallback");
    BOOST_TEST(root_redirect < fallback,
               "GET /{slug} must register before the /{path} fallback");
}

// Type: unit (redirect-surface pin; plan.md C10, security review MEDIUM: a new
//   redirect surface must be a reviewed edit to this test).
// Scenario: the set of redirect-tagged routes in registeredRoutes() is EXACTLY
//   {GET /{slug} -> redirect_root, GET /r/{slug} -> redirect_prefixed}, and the
//   only other routes sharing the `redirect_` metric label are the known
//   {GET,POST,DELETE} /{path} URI-store fallback (tagged "Fallback", not
//   "Redirects").
// Boundaries: adding, removing, renaming, or re-labeling a redirect route - or
//   adding any new route under the `redirect_` metric label - changes one of
//   the two pinned sets and fails here. The partition keeps the genuine
//   redirect surface separate from the fallback that merely shares the label.
// On failure first check: was a redirect endpoint added or a label changed? If
//   intended, update the expected sets here (reviewed); otherwise revert it.
BOOST_AUTO_TEST_CASE(redirect_route_set_is_pinned)
{
    const RedirectPartition parts = partition_redirects(registeredRoutes());

    const std::set<std::string> expected_redirects = {
        "GET /{slug} -> redirect_root",
        "GET /r/{slug} -> redirect_prefixed",
    };
    const std::set<std::string> expected_fallback = {
        "GET /{path} -> redirect_root",
        "POST /{path} -> redirect_root",
        "DELETE /{path} -> redirect_root",
    };

    BOOST_TEST((parts.tagged == expected_redirects),
               "redirect-tagged route set drifted; found { "
                   << join_set(parts.tagged) << " }, expected { "
                   << join_set(expected_redirects) << " }");
    BOOST_TEST((parts.labeled_only == expected_fallback),
               "redirect_-labeled (non-redirect) route set drifted; found { "
                   << join_set(parts.labeled_only) << " }, expected { "
                   << join_set(expected_fallback) << " }");
}

// Type: unit (composition-root guard; plan.md C11/C10).
// Scenario: redirect_handlers.cpp never names the command-runtime or
//   job-runner builder functions (build_command_runtime, process_job_runner),
//   ignoring `//` and `/* */` comments.
// Boundaries: calling build_command_runtime() from the redirect path fails
//   here; the redirect handlers resolve links through LinkBackend only.
// On failure first check: did redirect code try to build a registry/runtime?
//   Composition belongs to the one composition root, never a request handler.
BOOST_AUTO_TEST_CASE(redirect_handlers_do_not_construct_registry)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    const std::string code = strip_line_comments(readFile(redirect_cpp()));
    BOOST_REQUIRE_MESSAGE(!code.empty(), "redirect .cpp empty/unreadable");
    for (const std::string_view builder : k_registry_builders) {
        BOOST_TEST(code.find(builder) == std::string::npos,
                   "redirect .cpp calls composition/job builder '"
                       << builder
                       << "' (C11: only the composition root builds runtimes)");
    }
}

// Type: unit (dependency-direction guard; plan.md C10, dependency direction
//   registry -> services, never into HTTP redirect code).
// Scenario: no translation unit under src/commands/ includes
//   url_shortener/http/handlers/redirect_handlers.hpp.
// Boundaries: a command including the redirect header fails here. The scan
//   searches only for that one exact include string, so a partially written
//   sibling file (concurrent agent edits) cannot cause a false failure.
// On failure first check: which command started including the redirect header?
//   Command code depends on services (LinkBackend), never on HTTP handlers.
BOOST_AUTO_TEST_CASE(command_layer_does_not_depend_on_redirect_handlers)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    const fs::path commands_dir = root / "src" / "commands";
    if (!fs::exists(commands_dir)) {
        BOOST_TEST_MESSAGE("src/commands/ not present; nothing to scan");
        return;
    }

    constexpr std::string_view forbidden =
        "url_shortener/http/handlers/redirect_handlers.hpp";

    std::error_code ec;
    fs::recursive_directory_iterator it(
        commands_dir, fs::directory_options::skip_permission_denied, ec);
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
        const std::string code = readFile(entry.path());
        BOOST_TEST(code.find(forbidden) == std::string::npos,
                   "command source "
                       << entry.path().filename().string()
                       << " includes the redirect handler header "
                          "(dependency direction must be registry -> services, "
                          "never into HTTP redirect code)");
    }
}

// Type: unit (analytics best-effort guard; plan.md C10, CLAUDE.md "analytics
//   failures must never affect redirect response behavior or latency").
// Scenario: redirect_handlers.cpp still emits analytics through the file-local
//   emitClickEvent( -> analyticsClickRepository().InsertBatch( path (the
//   best-effort, drop-on-failure write present today), not via a command or
//   job.
// Boundaries: replacing emitClickEvent/InsertBatch with a command or job
//   submission removes these tokens and fails here, flagging that the analytics
//   write may have gained request-path coupling.
// On failure first check: was the redirect analytics write rerouted through a
//   command/job? Keep it a best-effort in-process emit off the hot path.
BOOST_AUTO_TEST_CASE(analytics_path_remains_best_effort)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    const std::string code = readFile(redirect_cpp());
    BOOST_REQUIRE_MESSAGE(!code.empty(), "redirect .cpp empty/unreadable");
    BOOST_TEST(code.find("emitClickEvent(") != std::string::npos,
               "redirect .cpp no longer emits analytics via emitClickEvent(");
    BOOST_TEST(code.find("analyticsClickRepository().InsertBatch(")
                   != std::string::npos,
               "redirect analytics no longer flows through "
               "analyticsClickRepository().InsertBatch( (best-effort emit)");
}

// Type: unit (negative self-test of the direct include/symbol guards).
// Scenario: a fixture carrying a forbidden include, a forbidden code symbol,
//   and the same token in both a `//` and a `/* */` comment is reported
//   forbidden by includesOf/is_forbidden_include and by the symbol scan, while
//   the two commented tokens are correctly ignored.
// Boundaries: proves the direct guards would actually catch a real violation
//   (file and line are reported) instead of passing vacuously, and proves both
//   comment forms are stripped; if include_target or the comment stripper
//   regressed, this case goes red.
// On failure first check: did include parsing or comment stripping (`//` or
//   `/* */`) stop working? Fix the helper; do not weaken the real guards above.
BOOST_AUTO_TEST_CASE(guard_flags_planted_violations)
{
    const TempDir dir("selftest");
    const fs::path tainted = dir.path() / "tainted.cpp";
    {
        std::ofstream out(tainted);
        out << "#include <string>\n";  // 1
        out << "#include <url_shortener/commands/command_registry.hpp>\n";  // 2
        out << "// CommandRegistry here is only a line comment\n";  // 3
        out << "/* ICommandClient only in a block comment */\n";  // 4
        out << "void f() { CommandRequest r; }\n";  // 5
    }

    // Include scan: the forbidden include is found with its exact line number.
    const auto includes = includesOf(tainted);
    bool found_forbidden_include = false;
    for (const auto& inc : includes) {
        if (is_forbidden_include(inc.target)) {
            found_forbidden_include = true;
            BOOST_TEST(inc.line == 2);
            BOOST_TEST(inc.target
                       == "url_shortener/commands/command_registry.hpp");
        }
    }
    BOOST_TEST(found_forbidden_include);

    // Symbol scan: CommandRequest (code) is caught; CommandRegistry (line
    // comment) and ICommandClient (block comment) are both stripped first.
    const std::string stripped = strip_line_comments(readFile(tainted));
    BOOST_TEST(stripped.find("CommandRequest") != std::string::npos);
    BOOST_TEST(stripped.find("CommandRegistry") == std::string::npos);
    BOOST_TEST(stripped.find("ICommandClient") == std::string::npos);
}

// Type: unit (negative self-test of the transitive include-closure guard).
// Scenario: a fixture seed .cpp includes an allowlist-shaped header
//   app/hop.hpp which itself includes url_shortener/commands/evil.hpp; the
//   closure walk reports the forbidden header with the full chain.
// Boundaries: proves forbidden_in_include_closure follows indirect edges (not
//   just the seed's direct includes) and surfaces the chain; a clean sibling
//   tree yields no violation.
// On failure first check: did the closure walk stop following url_shortener
//   includes or stop resolving them under the include root?
BOOST_AUTO_TEST_CASE(guard_flags_transitive_forbidden_include)
{
    const TempDir dir("closure");
    const fs::path include_root = dir.path() / "include";
    const fs::path seed = dir.path() / "seed.cpp";
    fs::create_directories(include_root / "url_shortener" / "app");
    fs::create_directories(include_root / "url_shortener" / "commands");
    {
        std::ofstream out(seed);
        out << "#include <url_shortener/app/hop.hpp>\n";
    }
    {
        std::ofstream out(include_root / "url_shortener" / "app" / "hop.hpp");
        out << "#include <url_shortener/commands/evil.hpp>\n";
    }
    {
        std::ofstream out(include_root / "url_shortener" / "commands"
                          / "evil.hpp");
        out << "#pragma once\n";
    }

    const auto violation = forbidden_in_include_closure({seed}, include_root);
    BOOST_REQUIRE(violation.has_value());
    BOOST_TEST(violation->target == "url_shortener/commands/evil.hpp");
    BOOST_TEST(violation->chain
               == "seed.cpp -> url_shortener/app/hop.hpp -> "
                  "url_shortener/commands/evil.hpp");

    // A clean tree (drop the forbidden leaf include) yields no violation.
    {
        std::ofstream out(include_root / "url_shortener" / "app" / "hop.hpp",
                          std::ios::trunc);
        out << "#pragma once\n";
    }
    BOOST_TEST(!forbidden_in_include_closure({seed}, include_root).has_value());
}

// Type: unit (negative self-test of the redirect-surface pin).
// Scenario: feeding partition_redirects a synthetic route list that adds a
//   planted redirect route (GET /evil, tag "Redirects") produces a tagged set
//   that differs from the pinned expectation, i.e. the pin would fail.
// Boundaries: proves the pin is not vacuous - a new redirect surface really
//   does change parts.tagged.
// On failure first check: did partition_redirects stop keying on the
//   "Redirects" tag or the redirect_ label?
BOOST_AUTO_TEST_CASE(guard_flags_redirect_route_drift)
{
    std::vector<RouteDescriptor> routes;

    RouteDescriptor good_root;
    good_root.method = "GET";
    good_root.path_pattern = "/{slug}";
    good_root.route_label = "redirect_root";
    good_root.tags = {"Redirects"};
    routes.push_back(good_root);

    RouteDescriptor planted;
    planted.method = "GET";
    planted.path_pattern = "/evil";
    planted.route_label = "redirect_prefixed";
    planted.tags = {"Redirects"};
    routes.push_back(planted);

    RouteDescriptor fallback;
    fallback.method = "GET";
    fallback.path_pattern = "/{path}";
    fallback.route_label = "redirect_root";
    fallback.tags = {"Fallback"};
    routes.push_back(fallback);

    const RedirectPartition parts = partition_redirects(routes);
    const std::set<std::string> pinned = {"GET /{slug} -> redirect_root"};
    // The planted redirect surface makes the tagged set exceed the pin.
    BOOST_TEST((parts.tagged != pinned));
    BOOST_TEST(parts.tagged.count("GET /evil -> redirect_prefixed") == 1U);
    // The fallback is partitioned away from the redirect surface.
    BOOST_TEST(parts.labeled_only.count("GET /{path} -> redirect_root") == 1U);
    BOOST_TEST(parts.tagged.count("GET /{path} -> redirect_root") == 0U);
}

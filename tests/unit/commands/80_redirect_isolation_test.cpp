#define BOOST_TEST_MODULE RedirectIsolationStructuralGuard
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/http/route_registry.hpp>

#include <filesystem>

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
/// set is a layering violation and fails `redirect_sources_include_allowlist_only`.
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

/// One `#include` line: its 1-based line number and the quoted/bracketed target.
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
    if (i == std::string::npos
        || line.compare(i, keyword.size(), keyword) != 0)
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

/// @brief Drops a `//` line comment from one source line.
///
/// Per the subtask constraint "token scans ignore comments only if they start
/// with `//`; do not try to parse C++", only `//` comments are removed; block
/// comments and string literals are left intact.
///
/// @param line A single source line.
/// @return @p line truncated at the first `//`, or unchanged when none.
std::string strip_line_comment(const std::string& line)
{
    const std::size_t pos = line.find("//");
    return pos == std::string::npos ? line : line.substr(0, pos);
}

/// @brief Returns the file text with every `//` line comment removed.
///
/// @param text Full source text.
/// @return The text with `//`-comment tails stripped, newlines preserved, so a
///         forbidden token appearing only in a `//` comment is not flagged.
std::string strip_line_comments(const std::string& text)
{
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        out << strip_line_comment(line) << '\n';
    }
    return out.str();
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
                       [&](std::string_view bad) {
                           return target.find(bad) != std::string::npos;
                       });
}

/// @brief Whether a `url_shortener/...` include is on the allowlist.
///
/// @param target An include target already known to start with `url_shortener/`.
/// @return `true` when @p target is one of
///         @ref k_allowed_url_shortener_includes.
bool is_allowed_url_shortener(const std::string& target)
{
    return std::any_of(k_allowed_url_shortener_includes.begin(),
                       k_allowed_url_shortener_includes.end(),
                       [&](std::string_view ok) { return target == ok; });
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
    const auto it =
        std::find_if(routes.begin(), routes.end(),
                     [&](const RouteDescriptor& route) {
                         return route.method == method
                             && route.path_pattern == path;
                     });
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

// Type: unit (textual symbol guard; plan.md C10).
// Scenario: the redirect sources contain none of the command/job/transport
//   identifiers (CommandRegistry, CommandRequest, ICommandClient, JobRunner,
//   ITransportAdapter, ArgSpec, execute(), ignoring `//` comments.
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
        BOOST_REQUIRE_MESSAGE(!code.empty(),
                              "redirect source empty/unreadable: "
                                  << file.string());
        for (const std::string_view symbol : k_forbidden_symbols) {
            BOOST_TEST(code.find(symbol) == std::string::npos,
                       "forbidden command-layer symbol '"
                           << symbol << "' in " << file.filename().string()
                           << " (C10: redirect fast path must not touch the "
                              "command/job/transport machinery)");
        }
    }
}

// Type: unit (route-table guard; plan.md C10).
// Scenario: the redirect routes GET /{slug} and GET /r/{slug} exist and carry
//   redirect metadata (route_label redirect_root / redirect_prefixed, tag
//   "Redirects") rather than any command classification (label api_commands or
//   tag "Commands"); i.e. they are not modeled as commands.
// Boundaries: re-labeling a redirect route to api_commands, or tagging it
//   "Commands", fails this. (The RouteDescriptor has no dedicated
//   command-mapping field in this milestone, so the route_label/tag are the
//   available command-association signal; see deviation note in the report.)
// On failure first check: did a route-table edit reclassify a redirect as a
//   command surface? Redirects belong to the fast path, not the command plane.
BOOST_AUTO_TEST_CASE(redirect_routes_not_in_command_route_table)
{
    const std::array<std::pair<std::string, std::string>, 2> redirects = {{
        {"GET", "/{slug}"},
        {"GET", "/r/{slug}"},
    }};

    for (const auto& [method, path] : redirects) {
        const RouteDescriptor* route = find_route(method, path);
        BOOST_REQUIRE_MESSAGE(route != nullptr,
                              "redirect route missing from registry: "
                                  << method << ' ' << path);

        BOOST_TEST(route->route_label != "api_commands",
                   "redirect route " << method << ' ' << path
                                     << " is labeled as a command surface");

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

// Type: unit (composition-root guard; plan.md C11/C10).
// Scenario: redirect_handlers.cpp never names the command-runtime or
//   job-runner builder functions (build_command_runtime, process_job_runner),
//   ignoring `//` comments.
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

// Type: unit (negative self-test of the guard itself).
// Scenario: a fixture written to a temp directory that carries a forbidden
//   include, a forbidden symbol, and a `//`-commented false token is reported
//   as forbidden by includesOf/is_forbidden_include and by the symbol scan,
//   while the commented token is correctly ignored.
// Boundaries: proves the guard would actually catch a real violation (file and
//   line are reported) instead of passing vacuously; if include_target or the
//   comment stripper regressed, this case goes red.
// On failure first check: did include parsing or `//` comment stripping stop
//   working? Fix the helper; do not weaken the real guards above.
BOOST_AUTO_TEST_CASE(guard_flags_planted_violations)
{
    const fs::path dir =
        fs::temp_directory_path() / "url_shortener_redirect_guard_selftest";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path tainted = dir / "tainted.cpp";
    {
        std::ofstream out(tainted);
        out << "#include <string>\n";                                   // 1
        out << "#include <url_shortener/commands/command_registry.hpp>\n";  // 2
        out << "// CommandRegistry here is only a comment\n";           // 3
        out << "void f() { CommandRequest r; }\n";                      // 4
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

    // Symbol scan: CommandRequest (code) is caught; CommandRegistry only ever
    // appears in a `//` comment and must therefore be ignored.
    const std::string stripped = strip_line_comments(readFile(tainted));
    BOOST_TEST(stripped.find("CommandRequest") != std::string::npos);
    BOOST_TEST(stripped.find("CommandRegistry") == std::string::npos);

    fs::remove_all(dir);
}

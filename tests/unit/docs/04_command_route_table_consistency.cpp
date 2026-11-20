#define BOOST_TEST_MODULE DocsCommandRouteTableConsistency
#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "doc_table.hpp"
#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/composition/command_runtime.hpp"
#include "url_shortener/composition/link_backend_bootstrap.hpp"
#include "url_shortener/core/config.hpp"
#include "url_shortener/http/route_registry.hpp"
#include "url_shortener/http/router_builder.hpp"

using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandRegistry;
using url_shortener::composition::build_command_runtime;
using url_shortener::composition::CommandRuntime;
using url_shortener::http::registeredRoutes;
using url_shortener::http::RouteDescriptor;
using url_shortener::http::RouterBuilder;

namespace
{

/// @brief Hint appended to every section-5 drift failure: the live routes come
/// from the C++ `RouteRegistry` (which also feeds `docs/api/README.md`), so the
/// fix is to re-align the hand-written route -> command table in
/// `docs/commands/README.md`, never to loosen this test.
constexpr const char* k_regen_hint =
    " (RouteRegistry/registeredRoutes() is the source of truth; re-align "
    "docs/commands/README.md section 5 by hand - this test never edits either "
    "side)";

/// @brief One `(method, path pattern)` pair parsed out of a section-5 route
/// cell.
struct RouteRef
{
    ///< HTTP method verb (e.g. `"GET"`, `"POST"`).
    std::string method;

    ///< URI pattern with `{param}` placeholders exactly as written in the
    ///< catalog (e.g. `"/api/v1/links/{slug}/enable"`).
    std::string path;
};

/// @brief Everything the drift checks need parsed out of section 5 of
/// `docs/commands/README.md`.
struct Section5
{
    ///< Every `(method, path)` route the table projects onto a command,
    ///< including the sibling rows folded into one cell
    ///< (enable/disable/restore, the three job routes). Preserves table order.
    std::vector<RouteRef> routes;

    ///< Every command name named in the `Command` column, first word only
    ///< (so `` `link.get {slug}` `` contributes `link.get`). The "any
    ///< (generic)" cell contributes nothing.
    std::set<std::string> commands;

    ///< Number of data rows read from the table (asserted > 0 so no check can
    ///< pass vacuously).
    std::size_t row_count = 0;

    ///< `true` once the `| REST route | Command |` table under the `## 5.`
    ///< heading was located and read.
    bool found_table = false;
};

/// @brief Tests whether @p word is an uppercase HTTP method verb.
///
/// @param word Candidate leading token of a route cell.
/// @return `true` for the verbs the route table can carry; `false` otherwise.
bool is_http_method(std::string_view word)
{
    return word == "GET" || word == "POST" || word == "PATCH"
        || word == "DELETE" || word == "PUT" || word == "HEAD"
        || word == "OPTIONS";
}

/// @brief Expands one section-5 route cell into its `(method, path)` pairs.
///
/// A cell lists one or more backtick tokens. A token of the form
/// `` `METHOD /path` `` is a full route and becomes the anchor. A bare token
/// without a space (e.g. `` `disable` ``) is a sibling of the current anchor:
/// its text replaces the final path segment of the anchor, keeping the anchor's
/// method (this is how `` `POST .../enable` / `disable` / `restore` `` packs
/// three routes into one cell). Tokens that are neither (a space-bearing token
/// whose first word is not a method) are ignored.
///
/// @param cell The raw `REST route` cell text.
/// @return The expanded routes in table order.
std::vector<RouteRef> routes_from_cell(std::string_view cell)
{
    std::vector<RouteRef> out;
    std::string anchor_method;
    std::string anchor_path;
    for (const std::string& token : doc_table::backtick_tokens(cell)) {
        const std::size_t space = token.find(' ');
        if (space != std::string::npos
            && is_http_method(token.substr(0, space)))
        {
            RouteRef ref;
            ref.method = token.substr(0, space);
            ref.path = doc_table::trim(token.substr(space + 1));
            anchor_method = ref.method;
            anchor_path = ref.path;
            out.push_back(std::move(ref));
        }
        else if (space == std::string::npos && !anchor_path.empty()) {
            const std::size_t slash = anchor_path.rfind('/');
            RouteRef ref;
            ref.method = anchor_method;
            ref.path = (slash == std::string::npos)
                ? token
                : anchor_path.substr(0, slash + 1) + token;
            out.push_back(std::move(ref));
        }
    }
    return out;
}

/// @brief Extracts the command names from one section-5 `Command` cell.
///
/// Each backtick token's first whitespace-delimited word that contains a `.`
/// is a command name (so `` `link.get {slug}` `` yields `link.get`). The
/// "any (generic)" cell has no backtick token and yields nothing.
///
/// @param cell The raw `Command` cell text.
/// @return The command names named by the cell.
std::vector<std::string> commands_from_cell(std::string_view cell)
{
    std::vector<std::string> out;
    for (const std::string& token : doc_table::backtick_tokens(cell)) {
        const std::string first = token.substr(0, token.find(' '));
        if (first.find('.') != std::string::npos) {
            out.push_back(first);
        }
    }
    return out;
}

/// @brief Parses section 5 (the route -> command table) of the catalog.
///
/// Single pass: find the `## 5.` heading, then the first
/// `| REST route | Command |` table before the next heading, then read each row
/// until the table ends. Column order is taken from the header, not assumed.
///
/// @param markdown The whole, CRLF-normalized catalog text.
/// @return The parsed section-5 model; `found_table` is false when the table is
/// absent.
Section5 parse_section5(const std::string& markdown)
{
    Section5 parsed;
    const std::vector<std::string> lines = doc_table::split_lines(markdown);

    std::size_t heading = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (doc_table::trim(lines[i]).rfind("## 5.", 0) == 0) {
            heading = i;
            break;
        }
    }
    if (heading == lines.size()) {
        return parsed;
    }

    for (std::size_t i = heading + 1; i < lines.size(); ++i) {
        const std::string t = doc_table::trim(lines[i]);
        if (t.rfind("## ", 0) == 0) {
            break;  // next section; table (if any) was not found
        }
        if (t.rfind("| ", 0) != 0) {
            continue;
        }
        const std::vector<std::string> header =
            doc_table::table_cells(lines[i]);
        std::size_t route_col = header.size();
        std::size_t cmd_col = header.size();
        for (std::size_t c = 0; c < header.size(); ++c) {
            if (header[c] == "REST route") {
                route_col = c;
            }
            else if (header[c] == "Command") {
                cmd_col = c;
            }
        }
        if (route_col == header.size() || cmd_col == header.size()) {
            continue;
        }
        parsed.found_table = true;
        for (std::size_t k = i + 2; k < lines.size(); ++k) {
            const std::string rt = doc_table::trim(lines[k]);
            if (rt.empty() || rt.front() != '|') {
                break;
            }
            if (doc_table::is_separator_row(lines[k])) {
                continue;
            }
            const std::vector<std::string> cells =
                doc_table::table_cells(lines[k]);
            if (cells.size() <= std::max(route_col, cmd_col)) {
                continue;
            }
            ++parsed.row_count;
            for (RouteRef& ref : routes_from_cell(cells[route_col])) {
                parsed.routes.push_back(std::move(ref));
            }
            for (std::string& name : commands_from_cell(cells[cmd_col])) {
                parsed.commands.insert(std::move(name));
            }
        }
        break;
    }
    return parsed;
}

/// @brief Tests whether a `(method, path)` pair is a registered route.
///
/// @param routes The live registry (`registeredRoutes()`).
/// @param method HTTP method to match.
/// @param path URI pattern to match.
/// @return `true` when the registry has a descriptor with exactly this method
/// and path pattern.
bool registry_has(const std::vector<RouteDescriptor>& routes,
                  const std::string& method,
                  const std::string& path)
{
    return std::any_of(
        routes.begin(),
        routes.end(),
        [&](const RouteDescriptor& r)
        { return r.method == method && r.path_pattern == path; });
}

/// @brief Tests whether a `(method, path)` pair appears in the parsed
/// section 5.
///
/// @param table The parsed route -> command table.
/// @param method HTTP method to match.
/// @param path URI pattern to match.
/// @return `true` when section 5 projects this exact route onto a command.
bool section5_has(const Section5& table,
                  const std::string& method,
                  const std::string& path)
{
    return std::any_of(table.routes.begin(),
                       table.routes.end(),
                       [&](const RouteRef& r)
                       { return r.method == method && r.path == path; });
}

/// @brief Tests whether a route belongs to the command family.
///
/// The command family is the canonical link API plus the generic command and
/// job surfaces - the routes section 5 is responsible for projecting. The
/// compatibility aliases (`/api/v1/short-urls`) and redirects are deliberately
/// excluded by not matching these prefixes.
///
/// @param path URI pattern of a registered route.
/// @return `true` when @p path is under `/api/v1/links`, `/api/v1/commands` or
/// `/api/v1/jobs`.
bool is_command_family(const std::string& path)
{
    return path.rfind("/api/v1/links", 0) == 0
        || path.rfind("/api/v1/commands", 0) == 0
        || path.rfind("/api/v1/jobs", 0) == 0;
}

/// @brief The production route registry, the production command registry, and
/// the committed catalog's parsed section 5.
///
/// Lifetime: @ref runtime owns the command registry and every object its
/// handlers reference; @ref command_registry is a non-owning pointer into it,
/// valid for the fixture's lifetime. Handlers are never invoked - only
/// descriptor names are read.
struct RouteTableFixture
{
    CommandRuntime runtime;
    const CommandRegistry* command_registry = nullptr;
    bool loaded = false;
    std::string source_dir;
    Section5 table;

    ///< `"METHOD path"` keys for every route the production RouterBuilder wires
    ///< up, so section-5 rows can be pinned to actually dispatchable routes
    ///< (and not just registry metadata).
    std::set<std::string> router_keys;

    RouteTableFixture()
    {
        url_shortener::app::LinkBackend::reset();
        ServerConfig server;
        url_shortener::app::LinkBackendHandle backend;  // inactive / legacy
        runtime = build_command_runtime(server, backend);
        command_registry = runtime.registry.get();

        const auto router = RouterBuilder::buildApplicationRouter();
        for (const auto& route : router.routes()) {
            router_keys.insert(route.method + ' ' + route.path_pattern);
        }

#ifdef SOURCE_DIR
        source_dir = SOURCE_DIR;
        const auto raw =
            doc_table::read_file(source_dir + "/docs/commands/README.md");
        if (raw.has_value()) {
            table = parse_section5(doc_table::normalize_crlf(*raw));
            loaded = true;
        }
#endif
    }

    /// @brief The set of registered command names.
    /// @return Every `CommandDescriptor::name` in the live registry.
    std::set<std::string> command_names() const
    {
        std::set<std::string> out;
        for (const CommandDescriptor* d : command_registry->list()) {
            out.insert(d->name);
        }
        return out;
    }
};

}  // namespace

// Type: unit (documentation-drift guard, bounded in-process parse).
// Scenario: section 5 of docs/commands/README.md (the hand-written route ->
//   command projection) is parsed once and cross-checked, in both directions,
//   against the live http::registeredRoutes() registry and the live
//   CommandRegistry that composition::build_command_runtime assembles the way
//   production does. Adding a command route without a section-5 row, or a row
//   that names a route/command the registries do not have, fails here.
// Boundaries: pure registry/doc inspection - no HTTP server, no socket, no
//   process spawn; handlers are never invoked; placeholder routes (/qr,
//   /routing) and compatibility aliases are excluded as section 3.6 states; the
//   generated api doc byte-equality is owned by docs__05.
// On failure first check: did a route or command change in the C++ registry
//   without updating docs/commands/README.md section 5 (fix the prose by hand),
//   or did a section-5 edit diverge from the registry? This test never rewrites
//   either side.

BOOST_FIXTURE_TEST_SUITE(command_route_table, RouteTableFixture)

// The fixture must find and parse the committed section-5 table, and both
// registries must be non-empty, so no case below can pass vacuously.
BOOST_AUTO_TEST_CASE(fixture_loaded_and_registries_nonempty)
{
#ifndef SOURCE_DIR
    BOOST_TEST_MESSAGE("SOURCE_DIR not defined; run via CTest. Skipping.");
#else
    BOOST_REQUIRE_MESSAGE(
        loaded, "could not read docs/commands/README.md under " << source_dir);
    BOOST_REQUIRE_MESSAGE(table.found_table,
                          "could not locate the section-5 route -> command "
                          "table in docs/commands/README.md");
    BOOST_REQUIRE_GT(table.row_count, 0u);
    BOOST_REQUIRE_GT(table.routes.size(), 0u);
    BOOST_REQUIRE(command_registry != nullptr);
    BOOST_REQUIRE_GT(command_registry->list().size(), 0u);
    BOOST_REQUIRE(!registeredRoutes().empty());
#endif
}

// Scenario: every (method, path) a section-5 row projects onto a command is a
//   real registered route AND a route the production RouterBuilder dispatches.
//   The "any (generic)" row's command column carries no route of its own and is
//   covered by its POST /api/v1/commands route cell.
// On failure first check: a section-5 route cell names a method+path that
//   RouteRegistry does not register or RouterBuilder does not wire up (typo, or
//   a route renamed/removed in C++ without updating the catalog).
BOOST_AUTO_TEST_CASE(table_rows_have_registered_routes)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    const auto& routes = registeredRoutes();
    for (const RouteRef& ref : table.routes) {
        BOOST_TEST(registry_has(routes, ref.method, ref.path),
                   "section 5: route '"
                       << ref.method << ' ' << ref.path
                       << "' is not registered in RouteRegistry"
                       << k_regen_hint);
        BOOST_TEST(router_keys.count(ref.method + ' ' + ref.path) == 1u,
                   "section 5: route '"
                       << ref.method << ' ' << ref.path
                       << "' is not dispatched by RouterBuilder"
                       << k_regen_hint);
    }
#endif
}

// Scenario: every command-family route the registry registers (canonical link
//   API, generic /api/v1/commands, /api/v1/jobs*), excluding the deliberate
//   placeholders, appears as a section-5 row, so a newly registered command
//   route cannot ship without a catalog row.
// Boundaries: placeholder routes (/qr, /routing, section 3.6) are excluded via
//   the descriptor's placeholder flag, not by path prefix alone; compatibility
//   aliases are outside the family prefixes and covered by a dedicated case.
// On failure first check: a route was added to RouteRegistry under the command
//   family without a matching section-5 row.
BOOST_AUTO_TEST_CASE(registered_command_routes_have_rows)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    for (const RouteDescriptor& route : registeredRoutes()) {
        if (!is_command_family(route.path_pattern) || route.placeholder) {
            continue;
        }
        BOOST_TEST(section5_has(table, route.method, route.path_pattern),
                   "section 5: registered command-family route '"
                       << route.method << ' ' << route.path_pattern
                       << "' has no route -> command row" << k_regen_hint);
    }
#endif
}

// Scenario: every command named in the section-5 Command column is a real
//   descriptor in CommandRegistry.
// On failure first check: a section-5 Command cell names a command that was
//   renamed or removed from the registry.
BOOST_AUTO_TEST_CASE(route_command_exists_in_registry)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    const std::set<std::string> known = command_names();
    for (const std::string& name : table.commands) {
        BOOST_TEST(known.count(name) == 1u,
                   "section 5: command '"
                       << name << "' is not a registered CommandDescriptor"
                       << k_regen_hint);
    }
#endif
}

// Scenario: operation ids on the registered routes are unique and follow the
//   existing lower_snake naming pattern (same rule as http 01_route_registry's
//   documentation-metadata case), so the new command/job routes cannot collide
//   with or diverge from the generated-reference identifiers.
// Boundaries: this asserts the C++ registry invariant the generated doc depends
//   on; it reads only registeredRoutes() (not the committed doc), so it runs
//   unconditionally - no SOURCE_DIR / loaded gate. docs__05 pins the rendered
//   bytes.
// On failure first check: a new route reused an operation id, left it empty, or
//   used characters outside [a-z0-9_].
BOOST_AUTO_TEST_CASE(operation_ids_unique_and_documented)
{
    std::set<std::string> seen;
    for (const RouteDescriptor& route : registeredRoutes()) {
        BOOST_TEST(!route.operation_id.empty(),
                   "empty operation_id for " << route.method << ' '
                                             << route.path_pattern);
        BOOST_TEST(seen.insert(route.operation_id).second,
                   "duplicate operation_id '" << route.operation_id << "'");
        for (const char c : route.operation_id) {
            const bool ok =
                (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            BOOST_TEST(ok,
                       "operation_id '" << route.operation_id
                                        << "' breaks the lower_snake pattern");
        }
    }
}

// Scenario: the compatibility aliases (/api/v1/short-urls*) and the prefixed
//   redirect (/r/{slug}) never appear in section 5 - they are explicitly not
//   commands (section 3.6 / plan).
// On failure first check: a compatibility alias or redirect was mistakenly
//   given a route -> command row.
BOOST_AUTO_TEST_CASE(compatibility_aliases_not_in_command_table)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    for (const RouteRef& ref : table.routes) {
        BOOST_TEST(ref.path.rfind("/api/v1/short-urls", 0) != 0u,
                   "section 5: compatibility alias '"
                       << ref.method << ' ' << ref.path
                       << "' must not be a command" << k_regen_hint);
        BOOST_TEST(ref.path != "/r/{slug}",
                   "section 5: prefixed redirect '/r/{slug}' must not be a "
                   "command"
                       << k_regen_hint);
    }
#endif
}

// Scenario: the redirect fast-path routes GET /{slug} and GET /r/{slug} are
//   registered routes yet carry no command mapping (contract C10 / ADR 0001) -
//   they must be absent from section 5.
// Boundaries: both routes must exist in RouteRegistry (so the absence check is
//   not vacuous) and both must be missing from the section-5 projection.
// On failure first check: a redirect route was given a route -> command row, or
//   a redirect route disappeared from RouteRegistry.
BOOST_AUTO_TEST_CASE(redirect_routes_not_commands)
{
#ifdef SOURCE_DIR
    if (!loaded) {
        return;
    }
    const auto& routes = registeredRoutes();
    BOOST_REQUIRE(registry_has(routes, "GET", "/{slug}"));
    BOOST_REQUIRE(registry_has(routes, "GET", "/r/{slug}"));
    BOOST_TEST(!section5_has(table, "GET", "/{slug}"),
               "section 5: redirect 'GET /{slug}' must not be a command"
                   << k_regen_hint);
    BOOST_TEST(!section5_has(table, "GET", "/r/{slug}"),
               "section 5: redirect 'GET /r/{slug}' must not be a command"
                   << k_regen_hint);
#endif
}

BOOST_AUTO_TEST_SUITE_END()

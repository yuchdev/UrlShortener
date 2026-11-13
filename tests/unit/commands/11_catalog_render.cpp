#define BOOST_TEST_MODULE CommandsCatalogRenderTest
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/catalog.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"

using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandResult;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::IJobContext;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobHandlerFactory;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::Permission;
using url_shortener::commands::render_catalog_markdown;
using url_shortener::commands::render_command_help;

namespace
{

// A trivial, never-failing sync handler used by the fixture descriptors.
CommandResult noop_handler(CommandContext&, const ArgsView&)
{
    CommandResult result;
    result.result_json = "{}";
    return result;
}

// A trivial job factory; registered but never invoked by the renderers.
JobHandlerFactory noop_job_factory()
{
    return [](const CommandContext&, const ArgsView&)
               -> std::variant<JobBody, CommandError> {
        return JobBody{[](IJobContext&) { return JobOutcome{}; }};
    };
}

// A mutating, dry-run-capable sync command exercising required/optional args,
// an enum with a default, list/map/object types and a "Maps to" line.
CommandDescriptor fixture_create()
{
    CommandDescriptor descriptor;
    descriptor.name = "item.create";
    descriptor.summary = "Create an item";
    descriptor.permission = Permission::write;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = true;
    descriptor.supports_dry_run = true;
    descriptor.handler = &noop_handler;
    descriptor.maps_to = {"POST /api/v1/items", "item create"};

    ArgSpec name;
    name.name = "name";
    name.type = ArgType::string;
    name.required = true;
    name.doc = "Display name";

    ArgSpec kind;
    kind.name = "kind";
    kind.type = ArgType::enumeration;
    kind.enum_values = {"a", "b"};
    kind.default_value = "\"a\"";
    kind.doc = "Kind of item";

    ArgSpec tags;
    tags.name = "tags";
    tags.type = ArgType::string_list;
    tags.doc = "Tags";

    ArgSpec meta;
    meta.name = "meta";
    meta.type = ArgType::string_map;
    meta.doc = "Metadata";

    ArgSpec extra;
    extra.name = "extra";
    extra.type = ArgType::object;
    extra.doc = "Extra object";

    descriptor.args = {name, kind, tags, meta, extra};
    return descriptor;
}

// A read/sync command with a mutually-exclusive one_of group.
CommandDescriptor fixture_get()
{
    CommandDescriptor descriptor;
    descriptor.name = "item.get";
    descriptor.summary = "Get an item";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.handler = &noop_handler;
    descriptor.maps_to = {"GET /api/v1/items/{slug}"};

    ArgSpec slug;
    slug.name = "slug";
    slug.type = ArgType::string;
    slug.one_of_group = "sel";
    slug.doc = "By slug";

    ArgSpec id;
    id.name = "id";
    id.type = ArgType::string;
    id.one_of_group = "sel";
    id.doc = "By id";

    descriptor.args = {slug, id};
    return descriptor;
}

// A read job command with no args and no "Maps to" line.
CommandDescriptor fixture_list()
{
    CommandDescriptor descriptor;
    descriptor.name = "item.list";
    descriptor.summary = "List items";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::job;
    descriptor.mutating = false;
    descriptor.job_factory = noop_job_factory();
    return descriptor;
}

// Populates a registry with the three fixtures (insertion order controllable).
void fill_registry(CommandRegistry& registry, bool reversed)
{
    if (reversed)
    {
        registry.add(fixture_list());
        registry.add(fixture_get());
        registry.add(fixture_create());
    }
    else
    {
        registry.add(fixture_create());
        registry.add(fixture_get());
        registry.add(fixture_list());
    }
}

// Asserts an output uses LF only, no tabs and no trailing whitespace per line.
void assert_clean_whitespace(const std::string& text)
{
    std::string line;
    for (const char c : text)
    {
        if (c == '\n')
        {
            BOOST_TEST((line.empty() || line.back() != ' '),
                       "trailing space on line: '" + line + "'");
            line.clear();
            continue;
        }
        BOOST_TEST(c != '\r', "carriage return in output");
        BOOST_TEST(c != '\t', "tab in output");
        line += c;
    }
    BOOST_TEST((line.empty() || line.back() != ' '),
               "trailing space on final line: '" + line + "'");
}

// Counts non-overlapping occurrences of needle in haystack.
std::size_t count_occurrences(const std::string& haystack,
                             const std::string& needle)
{
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++count;
        pos += needle.size();
    }
    return count;
}

const std::string kExpectedCatalog =
    "| Command | Perm | Kind | Mut | dry_run | Summary |\n"
    "|---|---|---|---|---|---|\n"
    "| `item.create` | `write` | sync | yes | yes | Create an item |\n"
    "| `item.get` | `read` | sync | no | no | Get an item |\n"
    "| `item.list` | `read` | job | no | no | List items |\n"
    "\n"
    "#### `item.create` - Perm `write`, sync, Mut, `dry_run` yes\n"
    "Create an item\n"
    "\n"
    "| Arg | Type | Req | Description |\n"
    "|---|---|---|---|\n"
    "| `name` | string | yes | Display name |\n"
    "| `kind` | enum `a`\\|`b` | no | Kind of item |\n"
    "| `tags` | list<string> | no | Tags |\n"
    "| `meta` | map<string,string> | no | Metadata |\n"
    "| `extra` | object | no | Extra object |\n"
    "\n"
    "Maps to: `POST /api/v1/items`, `item create`.\n"
    "\n"
    "#### `item.get` - Perm `read`, sync\n"
    "Get an item\n"
    "\n"
    "| Arg | Type | Req | Description |\n"
    "|---|---|---|---|\n"
    "| `slug` | string | one of | By slug |\n"
    "| `id` | string | one of | By id |\n"
    "\n"
    "Maps to: `GET /api/v1/items/{slug}`.\n"
    "\n"
    "#### `item.list` - Perm `read`, job\n"
    "List items\n";

}  // namespace

// ---------------------------------------------------------------------------
// Golden: the fixture registry renders to the exact expected Markdown, covering
// the index table, headings, argument tables and "Maps to" lines.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(catalog_matches_golden)
{
    CommandRegistry registry;
    fill_registry(registry, false);
    BOOST_TEST(render_catalog_markdown(registry) == kExpectedCatalog);
}

// ---------------------------------------------------------------------------
// Determinism: rendering twice, and rendering a registry built in reverse
// insertion order, yields byte-identical output (list() is name-sorted).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(catalog_is_deterministic)
{
    CommandRegistry forward;
    fill_registry(forward, false);
    CommandRegistry reversed;
    fill_registry(reversed, true);

    const std::string a = render_catalog_markdown(forward);
    const std::string b = render_catalog_markdown(forward);
    const std::string c = render_catalog_markdown(reversed);

    BOOST_TEST(a == b);
    BOOST_TEST(a == c);
}

// ---------------------------------------------------------------------------
// An empty registry renders only the (header-only) index table.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(catalog_empty_registry)
{
    CommandRegistry registry;
    const std::string expected =
        "| Command | Perm | Kind | Mut | dry_run | Summary |\n"
        "|---|---|---|---|---|---|\n";
    BOOST_TEST(render_catalog_markdown(registry) == expected);
}

// ---------------------------------------------------------------------------
// A doc containing '|' and a backtick is escaped and does not break the row.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(catalog_escapes_pipe_and_backtick)
{
    CommandDescriptor descriptor;
    descriptor.name = "esc.one";
    descriptor.summary = "escape fixture";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.handler = &noop_handler;

    ArgSpec weird;
    weird.name = "weird";
    weird.type = ArgType::string;
    weird.doc = "pipe | and `code` here";
    descriptor.args = {weird};

    CommandRegistry registry;
    registry.add(std::move(descriptor));

    const std::string markdown = render_catalog_markdown(registry);
    BOOST_TEST(markdown.find(
                   "| `weird` | string | no | pipe \\| and &#96;code&#96; "
                   "here |") != std::string::npos);
    // The escaped doc introduces no new unescaped pipe that would add a cell.
    BOOST_TEST(markdown.find("code` here") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Help: required/optional wording, enum values, default token, and the footer.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(help_create_matches_golden)
{
    const std::string expected =
        "Usage: url_shortener cmd item.create [--json '<args-object>' | "
        "--arg key=value ...]\n"
        "Create an item\n"
        "Arguments:\n"
        "  --arg name string required Display name\n"
        "  --arg kind enum optional [enum a|b] [default \"a\"] Kind of item\n"
        "  --arg tags list<string> optional Tags\n"
        "  --arg meta map<string,string> optional Metadata\n"
        "  --arg extra object optional Extra object\n"
        "Permission: write; mutating: yes; dry_run: yes; kind: sync\n";
    BOOST_TEST(render_command_help(fixture_create()) == expected);
}

// ---------------------------------------------------------------------------
// Help: a one_of group reads "required (one of: slug, id)" on each member.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(help_one_of_group_wording)
{
    const std::string expected =
        "Usage: url_shortener cmd item.get [--json '<args-object>' | "
        "--arg key=value ...]\n"
        "Get an item\n"
        "Arguments:\n"
        "  --arg slug string required (one of: slug, id) By slug\n"
        "  --arg id string required (one of: slug, id) By id\n"
        "Permission: read; mutating: no; dry_run: no; kind: sync\n";
    BOOST_TEST(render_command_help(fixture_get()) == expected);
}

// ---------------------------------------------------------------------------
// Help: a command with no args prints "Arguments: (none)".
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(help_no_args)
{
    const std::string expected =
        "Usage: url_shortener cmd item.list [--json '<args-object>' | "
        "--arg key=value ...]\n"
        "List items\n"
        "Arguments: (none)\n"
        "Permission: read; mutating: no; dry_run: no; kind: job\n";
    BOOST_TEST(render_command_help(fixture_list()) == expected);
}

// ---------------------------------------------------------------------------
// Help output names every ArgSpec exactly once (success criterion 3).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(help_names_every_arg_once)
{
    const CommandDescriptor descriptor = fixture_create();
    const std::string help = render_command_help(descriptor);
    for (const ArgSpec& arg : descriptor.args)
    {
        BOOST_TEST(count_occurrences(help, "--arg " + arg.name + " ") == 1u,
                   "arg not named exactly once: " + arg.name);
    }
}

// ---------------------------------------------------------------------------
// Neither renderer emits '\r', a tab, or any trailing whitespace.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(outputs_have_clean_whitespace)
{
    CommandRegistry registry;
    fill_registry(registry, false);
    assert_clean_whitespace(render_catalog_markdown(registry));
    assert_clean_whitespace(render_command_help(fixture_create()));
    assert_clean_whitespace(render_command_help(fixture_get()));
    assert_clean_whitespace(render_command_help(fixture_list()));
}

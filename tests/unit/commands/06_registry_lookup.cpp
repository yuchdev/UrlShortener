#define BOOST_TEST_MODULE CommandsRegistryLookupTest
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"

using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::CommandResult;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobHandlerFactory;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::Permission;

namespace
{

// A trivial, never-failing sync handler; its contents are irrelevant to these
// registration/lookup tests.
CommandResult noop_handler(CommandContext&, const ArgsView&)
{
    CommandResult result;
    result.result_json = "{}";
    return result;
}

// A trivial job factory that returns an empty body; never invoked here.
JobHandlerFactory noop_job_factory()
{
    return [](const CommandContext&, const ArgsView&)
               -> std::variant<JobBody, CommandError> {
        return JobBody{[](url_shortener::commands::IJobContext&) {
            return JobOutcome{};
        }};
    };
}

// Builds a minimally-valid synchronous descriptor with the given name.
CommandDescriptor valid_sync(std::string name)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "a summary";
    descriptor.handler = &noop_handler;
    return descriptor;
}

// Builds a minimally-valid read-only job descriptor with the given name.
CommandDescriptor valid_job(std::string name)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "a summary";
    descriptor.kind = ExecutionKind::job;
    descriptor.job_factory = noop_job_factory();
    return descriptor;
}

// Matches a std::invalid_argument whose message equals @p expected.
struct message_is
{
    std::string expected;

    bool operator()(const std::invalid_argument& error) const
    {
        return expected == error.what();
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// add + find round trip and misses.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(add_then_find_round_trip)
{
    CommandRegistry registry;
    registry.add(valid_sync("link.create"));

    const CommandDescriptor* found = registry.find("link.create");
    BOOST_REQUIRE(found != nullptr);
    BOOST_TEST(found->name == "link.create");
}

BOOST_AUTO_TEST_CASE(find_unknown_and_empty_name_return_null)
{
    CommandRegistry registry;
    registry.add(valid_sync("link.create"));

    BOOST_TEST(registry.find("link.delete") == nullptr);
    BOOST_TEST(registry.find("") == nullptr);
}

// ---------------------------------------------------------------------------
// list() is name-sorted regardless of insertion order and stable across calls.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(list_is_sorted_and_stable)
{
    CommandRegistry registry;
    registry.add(valid_sync("link.create"));
    registry.add(valid_sync("db.tables"));
    registry.add(valid_sync("system.info"));
    registry.add(valid_sync("link.get"));

    const std::vector<const CommandDescriptor*> first = registry.list();
    BOOST_REQUIRE_EQUAL(first.size(), 4u);
    BOOST_TEST(first[0]->name == "db.tables");
    BOOST_TEST(first[1]->name == "link.create");
    BOOST_TEST(first[2]->name == "link.get");
    BOOST_TEST(first[3]->name == "system.info");

    // Stable: a second call yields the same order and the same pointers.
    const std::vector<const CommandDescriptor*> second = registry.list();
    BOOST_REQUIRE_EQUAL(second.size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        BOOST_TEST(first[i] == second[i]);
    }
}

// ---------------------------------------------------------------------------
// Registration rejections (std::invalid_argument), with the reason asserted.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(reject_duplicate_name)
{
    CommandRegistry registry;
    registry.add(valid_sync("link.create"));
    BOOST_CHECK_EXCEPTION(registry.add(valid_sync("link.create")),
                          std::invalid_argument,
                          message_is{"duplicate command name"});
}

BOOST_AUTO_TEST_CASE(reject_invalid_name_uppercase)
{
    CommandRegistry registry;
    BOOST_CHECK_EXCEPTION(registry.add(valid_sync("Link.create")),
                          std::invalid_argument,
                          message_is{"invalid command name"});
}

BOOST_AUTO_TEST_CASE(reject_invalid_name_single_segment)
{
    CommandRegistry registry;
    BOOST_CHECK_EXCEPTION(registry.add(valid_sync("link")),
                          std::invalid_argument,
                          message_is{"invalid command name"});
}

BOOST_AUTO_TEST_CASE(reject_empty_summary)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_sync("link.create");
    descriptor.summary.clear();
    BOOST_CHECK_EXCEPTION(registry.add(std::move(descriptor)),
                          std::invalid_argument,
                          message_is{"empty summary"});
}

BOOST_AUTO_TEST_CASE(reject_null_handler_on_sync_command)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_sync("link.create");
    descriptor.handler = nullptr;
    BOOST_CHECK_EXCEPTION(registry.add(std::move(descriptor)),
                          std::invalid_argument,
                          message_is{"null handler for sync command"});
}

BOOST_AUTO_TEST_CASE(reject_job_without_factory)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_job("link.export");
    descriptor.job_factory = nullptr;
    BOOST_CHECK_EXCEPTION(registry.add(std::move(descriptor)),
                          std::invalid_argument,
                          message_is{"missing job_factory"});
}

BOOST_AUTO_TEST_CASE(reject_async_capable_without_factory)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_sync("link.create");
    descriptor.async_capable = true;
    BOOST_CHECK_EXCEPTION(registry.add(std::move(descriptor)),
                          std::invalid_argument,
                          message_is{"missing job_factory"});
}

BOOST_AUTO_TEST_CASE(reject_duplicate_arg_name)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_sync("link.create");
    ArgSpec first;
    first.name = "url";
    ArgSpec second;
    second.name = "url";
    descriptor.args = {first, second};
    BOOST_CHECK_EXCEPTION(registry.add(std::move(descriptor)),
                          std::invalid_argument,
                          message_is{"duplicate arg name"});
}

BOOST_AUTO_TEST_CASE(reject_dry_run_on_non_mutating_command)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_sync("link.get");
    descriptor.supports_dry_run = true;
    descriptor.mutating = false;
    BOOST_CHECK_EXCEPTION(registry.add(std::move(descriptor)),
                          std::invalid_argument,
                          message_is{"supports_dry_run requires mutating"});
}

// ---------------------------------------------------------------------------
// A read-only job descriptor with a factory is accepted (kind==job does not
// imply mutating).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(accept_read_only_job_with_factory)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_job("link.export");
    descriptor.mutating = false;
    BOOST_CHECK_NO_THROW(registry.add(std::move(descriptor)));

    const CommandDescriptor* found = registry.find("link.export");
    BOOST_REQUIRE(found != nullptr);
    BOOST_TEST((found->kind == ExecutionKind::job));
    BOOST_TEST(found->mutating == false);
}

// A dry_run sync command is accepted when it is mutating.
BOOST_AUTO_TEST_CASE(accept_dry_run_when_mutating)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_sync("link.delete");
    descriptor.mutating = true;
    descriptor.permission = Permission::write;
    descriptor.supports_dry_run = true;
    BOOST_CHECK_NO_THROW(registry.add(std::move(descriptor)));
    BOOST_TEST(registry.find("link.delete") != nullptr);
}

// Scenario: a mutating command with read base permission and no raise hook
// would let a read-only User change state (security review MEDIUM on 01.0/06).
BOOST_AUTO_TEST_CASE(reject_mutating_command_with_read_permission)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_sync("link.delete");
    descriptor.mutating = true;
    descriptor.permission = Permission::read;
    BOOST_CHECK_EXCEPTION(registry.add(std::move(descriptor)),
                          std::invalid_argument,
                          message_is{"mutating requires write permission"});
}

// Scenario: a read base is allowed for a mutating command only when a
// permission_for hook can raise it per call (decision D16).
BOOST_AUTO_TEST_CASE(accept_mutating_read_base_with_raise_hook)
{
    CommandRegistry registry;
    CommandDescriptor descriptor = valid_sync("link.export");
    descriptor.mutating = true;
    descriptor.permission = Permission::read;
    descriptor.permission_for = [](const ArgsView&) {
        return Permission::write;
    };
    BOOST_CHECK_NO_THROW(registry.add(std::move(descriptor)));
}


// ---------------------------------------------------------------------------
// Pointers returned by find stay valid across later add calls (node stability).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(find_pointer_stable_across_inserts)
{
    CommandRegistry registry;
    registry.add(valid_sync("ns.anchor"));
    const CommandDescriptor* anchor = registry.find("ns.anchor");
    BOOST_REQUIRE(anchor != nullptr);

    for (int i = 0; i < 100; ++i)
    {
        registry.add(valid_sync("ns.cmd" + std::to_string(i)));
    }

    // Same node, still readable.
    BOOST_TEST(registry.find("ns.anchor") == anchor);
    BOOST_TEST(anchor->name == "ns.anchor");
}

// ---------------------------------------------------------------------------
// The registry is non-copyable (handlers may capture `this`).
// ---------------------------------------------------------------------------

static_assert(!std::is_copy_constructible_v<CommandRegistry>,
              "CommandRegistry must not be copy-constructible");
static_assert(!std::is_copy_assignable_v<CommandRegistry>,
              "CommandRegistry must not be copy-assignable");

// Note: `execute`/`execute_json` behavior is covered by
// tests/unit/commands/07_execute_pipeline.cpp; the former stub case that
// expected an `internal` response here was removed when subtask 07 implemented
// the real pipeline.

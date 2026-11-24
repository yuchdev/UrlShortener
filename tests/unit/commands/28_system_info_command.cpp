#define BOOST_TEST_MODULE CommandsSystemInfoCommandTest
#include <string>

#include <boost/test/unit_test.hpp>

#include "url_shortener/app/system_info.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/system_commands.hpp"
#include "url_shortener/config/storage_config.hpp"
#include "url_shortener/core/config.hpp"

using url_shortener::app::make_system_info;
using url_shortener::app::SystemInfo;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::make_system_info_descriptor;

namespace
{

// Returns true when `haystack` contains `needle` as a substring.
bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// A caller with the given role; system.info requires only Permission::read, so
// both Admin and User are authorized -- the role only changes read_only.
CallerContext caller(ControlSet role)
{
    CallerContext c;
    c.principal = "test-principal";
    c.transport = "test";
    c.control_set = role;
    return c;
}

CommandRequest request(std::string args_json)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = "system.info";
    req.args_json = std::move(args_json);
    return req;
}

// A snapshot with distinctive, allowlisted values so the serialized result is
// easy to recognize (sqlite -> persistent=true).
SystemInfo sample_info()
{
    StorageConfig storage;
    storage.metadata.backend = "sqlite";
    storage.cache.backend = "redis";
    storage.rate_limit.enabled = true;
    storage.rate_limit.backend = "redis";
    return make_system_info(storage, ServerConfig {});
}

// Registers the system.info descriptor over sample_info() on `registry`. The
// registry is non-copyable/non-movable, so each test owns a local instance and
// hands it here rather than receiving one by value.
void install_system_info(CommandRegistry& registry)
{
    registry.add(make_system_info_descriptor(sample_info()));
}

}  // namespace

// Type: unit (system.info success shape).
// Scenario: a read caller invoking system.info with no args gets the full
//   result shape: version, the four backend names, persistent, read_only and
//   the shortener block.
// Boundaries: no args are required; the result is the serialized snapshot.
// On failure first check: did the descriptor lose a field, or the handler stop
//   serializing the snapshot?
BOOST_AUTO_TEST_CASE(system_info_success_shape)
{
    CommandRegistry registry;
    install_system_info(registry);

    const CommandResponse response =
        registry.execute(caller(ControlSet::Admin), request("{}"));

    BOOST_TEST(response.ok);
    BOOST_TEST(contains(response.result_json, "\"version\":\"1.0.0\""));
    BOOST_TEST(contains(response.result_json, "\"metadata\":\"sqlite\""));
    BOOST_TEST(contains(response.result_json, "\"cache\":\"redis\""));
    BOOST_TEST(contains(response.result_json, "\"analytics\":\"noop\""));
    BOOST_TEST(contains(response.result_json, "\"rate_limit\":\"redis\""));
    BOOST_TEST(contains(response.result_json, "\"persistent\":true"));
    BOOST_TEST(contains(response.result_json, "\"shortener\":{"));
}

// Type: unit (read_only derives from the caller's ControlSet).
// Scenario: the same command yields read_only:false for an Admin (can write)
//   and read_only:true for a User (read-only).
// Boundaries: read_only is a function of canWrite(control_set), not the
//   snapshot.
// On failure first check: did the handler stop reading context.caller, or
//   invert the canWrite check?
BOOST_AUTO_TEST_CASE(read_only_follows_control_set)
{
    CommandRegistry registry;
    install_system_info(registry);

    const CommandResponse admin =
        registry.execute(caller(ControlSet::Admin), request("{}"));
    BOOST_TEST(admin.ok);
    BOOST_TEST(contains(admin.result_json, "\"read_only\":false"));

    const CommandResponse user =
        registry.execute(caller(ControlSet::User), request("{}"));
    BOOST_TEST(user.ok);
    BOOST_TEST(contains(user.result_json, "\"read_only\":true"));
}

// Type: unit (closed schema: an unknown argument is rejected).
// Scenario: system.info takes no arguments, so any supplied field fails
//   invalid_field before the handler runs.
// Boundaries: the empty ArgSpec set is a closed schema.
// On failure first check: did the descriptor gain an argument, or validation
//   stop rejecting unknown fields?
BOOST_AUTO_TEST_CASE(unknown_argument_is_invalid_field)
{
    CommandRegistry registry;
    install_system_info(registry);

    const CommandResponse response =
        registry.execute(caller(ControlSet::User), request(R"({"verbose":1})"));

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::invalid_field));
}

// Type: unit (dry_run is unsupported on a non-mutating command).
// Scenario: a dry_run request for system.info fails unsupported because the
//   descriptor is not supports_dry_run.
// Boundaries: dry_run needs supports_dry_run, which a read command never has.
// On failure first check: did the descriptor flip supports_dry_run or mutating?
BOOST_AUTO_TEST_CASE(dry_run_is_unsupported)
{
    CommandRegistry registry;
    install_system_info(registry);

    CommandRequest req = request("{}");
    req.options.dry_run = true;
    const CommandResponse response =
        registry.execute(caller(ControlSet::Admin), req);

    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::unsupported));
}

// Type: unit (result is a stable snapshot across calls).
// Scenario: two identical invocations return byte-identical result JSON because
//   the descriptor captured the snapshot by value.
// Boundaries: the result depends only on the captured snapshot and the caller.
// On failure first check: did the handler start reading mutable/live state?
BOOST_AUTO_TEST_CASE(result_is_stable_across_calls)
{
    CommandRegistry registry;
    install_system_info(registry);

    const CommandResponse first =
        registry.execute(caller(ControlSet::Admin), request("{}"));
    const CommandResponse second =
        registry.execute(caller(ControlSet::Admin), request("{}"));

    BOOST_TEST(first.ok);
    BOOST_TEST(second.ok);
    BOOST_TEST(first.result_json == second.result_json);
}

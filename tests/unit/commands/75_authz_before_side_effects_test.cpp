#define BOOST_TEST_MODULE CommandsAuthzBeforeSideEffectsTest
#include <atomic>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <boost/test/unit_test.hpp>

#include "commands_test_fakes.hpp"
#include "url_shortener/app/link_command_service.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_submitter.hpp"
#include "url_shortener/commands/link_commands.hpp"
#include "url_shortener/commands/pair_commands.hpp"
#include "url_shortener/security/control_set.hpp"

using url_shortener::app::LinkCommandService;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::IJobSubmitter;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobInfo;
using url_shortener::commands::JobState;
using url_shortener::commands::JobSubmitResult;
using url_shortener::commands::LinkListFilter;
using url_shortener::commands::LinkPage;
using url_shortener::commands::make_link_export_descriptor;
using url_shortener::commands::make_link_import_descriptor;
using url_shortener::commands::PairCommandDeps;
using url_shortener::commands::register_link_commands;
using commands_test_fakes::FakeLinkStore;
using commands_test_fakes::FakeStatsReader;
using commands_test_fakes::make_test_config;

namespace
{

// A job submitter that counts calls and never actually runs a body, so a test
// can prove a denied call reaches submit() exactly zero times (the "no job
// enqueued" proof) while an allowed call still gets a job-accepted envelope.
class CountingJobSubmitter final : public IJobSubmitter
{
public:
    std::atomic<int> submit_calls {0};

    JobSubmitResult submit(std::string /*command*/,
                           std::string /*args_digest*/,
                           JobBody /*body*/) override
    {
        submit_calls.fetch_add(1);
        JobSubmitResult result;
        JobInfo info;
        info.id = "job-75";
        info.state = JobState::queued;
        result.job = info;
        return result;
    }
};

// Bundles a counting link store, the service over it, the registered link and
// pair commands, and a counting job submitter, so every side-effect proof reads
// the one store/submitter the handlers actually touch.
struct Harness
{
    FakeLinkStore store;
    FakeStatsReader stats;
    ServerConfig config = make_test_config();
    std::shared_ptr<LinkCommandService> service =
        std::make_shared<LinkCommandService>(store, stats, config);
    std::shared_ptr<CountingJobSubmitter> submitter =
        std::make_shared<CountingJobSubmitter>();
    CommandRegistry registry;

    Harness()
    {
        register_link_commands(registry, *service);

        PairCommandDeps deps;
        deps.service = service;
        deps.max_inline_bytes = config.max_request_body_bytes;
        // An empty page source: link.export is registered (export available),
        // but a denied call never reaches it, and an allowed inline export pages
        // an empty result.
        deps.list_page = [](const LinkListFilter&, std::size_t, std::size_t)
        {
            return url_shortener::app::Result<LinkPage> {LinkPage {}, {}};
        };
        registry.add(make_link_import_descriptor(deps));
        registry.add(make_link_export_descriptor(deps));

        registry.set_job_submitter(submitter);
    }

    CommandResponse run(const CallerContext& caller, const CommandRequest& req)
    {
        return registry.execute(caller, req);
    }
};

CallerContext admin_caller()
{
    CallerContext caller;
    caller.principal = "test-admin";
    caller.transport = "test";
    caller.control_set = ControlSet::Admin;
    return caller;
}

CallerContext user_caller()
{
    CallerContext caller;
    caller.principal = "test-user";
    caller.transport = "test";
    caller.control_set = ControlSet::User;
    return caller;
}

CommandRequest request(std::string command,
                       std::string args_json,
                       bool dry_run = false)
{
    CommandRequest req;
    req.id = "client-1";
    req.command = std::move(command);
    req.args_json = std::move(args_json);
    req.options.dry_run = dry_run;
    return req;
}

bool file_exists(const std::string& path)
{
    std::ifstream stream(path);
    return stream.good();
}

}  // namespace

// Type: unit (a denied write never touches the repository, C7/T-116).
// Scenario: an Admin seeds one link; then a read-only User is denied
//   link.create, link.update and link.delete, each with not_authorized. The
//   counting store shows one create call total (the seed), its size stays one,
//   and the seeded link's target is unchanged -- the denied calls reached no
//   store method.
// Boundaries: authorization fires in execute before dispatch, so the handler --
//   and therefore the store -- is never entered on denial.
// On failure first check: did a write descriptor drop its write permission, or
//   did a denied path still reach the store?
BOOST_AUTO_TEST_CASE(denied_call_does_not_touch_repository)
{
    Harness h;

    const std::string slug = "seed-75";
    BOOST_REQUIRE(
        h.run(admin_caller(),
              request("link.create",
                      R"({"target_url":"https://example.com/seed","slug":")"
                          + slug + R"("})",
                      false))
            .ok);
    BOOST_REQUIRE_EQUAL(h.store.size(), 1u);
    const std::size_t create_calls_after_seed = h.store.create_calls();
    const std::size_t update_calls_after_seed = h.store.update_calls();
    const auto seeded = h.store.findBySlug(slug);
    BOOST_REQUIRE(seeded.has_value());
    const std::string seeded_target = seeded->target_url;

    const CommandRequest denied[] = {
        request("link.create",
                R"({"target_url":"https://example.com/x","slug":"denied-75"})",
                false),
        request("link.update",
                R"({"slug":")" + slug + R"(","redirect_type":"permanent"})",
                false),
        request("link.delete", R"({"slug":")" + slug + R"("})", false),
    };

    for (const CommandRequest& req : denied) {
        const CommandResponse response = h.run(user_caller(), req);
        BOOST_TEST(!response.ok, "write must be denied: " << req.command);
        BOOST_TEST((response.error.code == CommandErrorCode::not_authorized),
                   "expected not_authorized for " << req.command);
    }

    // Zero touches: no mutating repository call fired. create_calls counts
    // link.create; update_calls counts the write path link.update/link.delete
    // persist through (ILinkStore soft-deletes via update, no distinct delete
    // method), so both staying at their pre-denial value proves the denied
    // create, update AND delete reached no store method.
    BOOST_TEST(h.store.create_calls() == create_calls_after_seed);
    BOOST_TEST(h.store.update_calls() == update_calls_after_seed);
    // The state is also byte-identical: the size is unchanged and the seeded
    // link's content is intact.
    BOOST_TEST(h.store.size() == 1u);
    const auto reread = h.store.findBySlug(slug);
    BOOST_REQUIRE(reread.has_value());
    BOOST_TEST(reread->target_url == seeded_target);
}

// Type: unit (a denied job command never enqueues a job, T-12/T-116).
// Scenario: a read-only User runs link.import (a write, kind==job) and is denied
//   with not_authorized; the counting submitter's submit() is never called, so
//   no job is enqueued and the factory is never built. Empty args are enough
//   because the base write gate fires before argument validation and before the
//   job factory/submitter path.
// Boundaries: the submitter is the real seam the registry hands bodies to; a
//   denial short-circuits before it.
// On failure first check: did link.import lose its write permission, or did the
//   job path run before authorization?
BOOST_AUTO_TEST_CASE(denied_call_does_not_enqueue_job)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request("link.import", "{}", false));
    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(h.submitter->submit_calls.load() == 0);
}

// Type: unit (authorization precedes argument validation, T-08).
// Scenario: a read-only User submits link.create with invalid args (missing the
//   required target_url and an unknown key); the response is not_authorized, not
//   invalid_field, and nothing is created.
// Boundaries: execute checks the base permission at step 3, before validate_args
//   at step 4, so an unauthorized caller learns nothing about the schema.
// On failure first check: did validation move ahead of the permission gate?
BOOST_AUTO_TEST_CASE(authorization_precedes_argspec_validation)
{
    Harness h;

    const CommandResponse response = h.run(
        user_caller(), request("link.create", R"({"bogus":"x"})", false));
    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(h.store.create_calls() == 0u);
}

// Type: unit (an unknown command is resolved before authorization, pipeline
//   ordering).
// Scenario: a read-only User names a command that does not exist; the response
//   is unknown_command, not not_authorized -- lookup (step 2) precedes the
//   permission gate (step 3), and the unknown name is not echoed.
// Boundaries: a non-existent descriptor has no permission to check, so the
//   lookup result must win.
// On failure first check: did the permission gate move ahead of the registry
//   lookup?
BOOST_AUTO_TEST_CASE(unknown_command_precedes_authorization)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(), request("no.such.command", "{}", false));
    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::unknown_command));
}

// Type: unit (a dry_run still requires permission, T-21/T-57).
// Scenario: a read-only User runs link.create with options.dry_run; the
//   response is not_authorized, not a dry_run success -- permission is enforced
//   before the dispatch-mode resolution, so a dry_run cannot be used to probe a
//   write the caller may not perform. Nothing is created.
// Boundaries: the dry_run path runs only after steps 3-4a, so an unauthorized
//   dry_run is denied, never "unsupported" or a validated no-op.
// On failure first check: did the dry_run branch skip the permission gate?
BOOST_AUTO_TEST_CASE(dry_run_still_requires_permission)
{
    Harness h;

    const CommandResponse response =
        h.run(user_caller(),
              request("link.create",
                      R"({"target_url":"https://example.com/d","slug":"dry-75"})",
                      true));
    BOOST_TEST(!response.ok);
    BOOST_TEST((response.error.code == CommandErrorCode::not_authorized));
    BOOST_TEST(h.store.create_calls() == 0u);
}

// Type: unit (a read-only export to a path is denied; inline export is allowed,
//   T-9/T-118).
// Scenario: a read-only User running link.export with a `path` argument is
//   denied with not_authorized (the path raise makes it a write), and the
//   target file is never created -- the denial precedes the export factory, so
//   the filesystem is never touched. The same User running an inline export (no
//   path) is NOT denied (it stays read), proving the raise is scoped to the
//   path.
// Boundaries: the permission_for hook raises read->write on `path`
//   (pair_commands.cpp:1063) before dispatch, so a denied path export never
//   reaches the factory, the path policy, or the sink.
// On failure first check: did the export path raise stop firing, or did a
//   denied export still open the file?
BOOST_AUTO_TEST_CASE(read_only_export_to_path_denied)
{
    Harness h;

    const std::string sentinel = "./authz75_export_should_not_exist.csv";
    std::remove(sentinel.c_str());
    BOOST_REQUIRE(!file_exists(sentinel));

    const CommandResponse path_export = h.run(
        user_caller(),
        request("link.export",
                R"({"format":"csv","path":")" + sentinel + R"("})",
                false));
    BOOST_TEST(!path_export.ok);
    BOOST_TEST((path_export.error.code == CommandErrorCode::not_authorized));
    // Denied before the factory: no file was opened or created.
    BOOST_TEST(!file_exists(sentinel));
    BOOST_TEST(h.submitter->submit_calls.load() == 0);

    // Inline export (no path) stays read, so the same User is not denied.
    const CommandResponse inline_export = h.run(
        user_caller(), request("link.export", R"({"format":"csv"})", false));
    BOOST_TEST((inline_export.error.code != CommandErrorCode::not_authorized),
               "inline export must be allowed for a read-only User");
}

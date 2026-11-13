#define BOOST_TEST_MODULE CommandsAuditSinkTest
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/command_descriptor.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_submitter.hpp"
#include "url_shortener/observability/logger.hpp"

using url_shortener::commands::ArgSpec;
using url_shortener::commands::ArgsView;
using url_shortener::commands::ArgType;
using url_shortener::commands::AuditRecord;
using url_shortener::commands::audit_fields;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandContext;
using url_shortener::commands::CommandDescriptor;
using url_shortener::commands::CommandError;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandHandler;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::CommandResult;
using url_shortener::commands::ExecutionKind;
using url_shortener::commands::IAuditSink;
using url_shortener::commands::IJobContext;
using url_shortener::commands::IJobSubmitter;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobHandlerFactory;
using url_shortener::commands::JobInfo;
using url_shortener::commands::JobOutcome;
using url_shortener::commands::JobState;
using url_shortener::commands::JobSubmitResult;
using url_shortener::commands::LoggerAuditSink;
using url_shortener::commands::make_error;
using url_shortener::commands::Permission;
using url_shortener::observability::LogField;
using url_shortener::observability::Logger;

namespace
{

// Captures every AuditRecord the registry emits. Lives on the test stack and
// is installed via a non-owning shared_ptr aliasing it, so the registry shares
// but never destroys it.
struct CapturingAuditSink : public IAuditSink
{
    // Every record received, in emission order.
    std::vector<AuditRecord> records;

    void record(const AuditRecord& record) noexcept override
    {
        records.push_back(record);
    }
};

// A submitter that accepts with a canned job (default "j-7"/queued) or rejects
// with a canned error, for the job_accepted / busy audit cases.
struct FakeJobSubmitter : public IJobSubmitter
{
    JobSubmitResult canned;

    FakeJobSubmitter()
    {
        JobInfo info;
        info.id = "j-7";
        info.state = JobState::queued;
        canned.job = info;
    }

    JobSubmitResult submit(std::string, std::string, JobBody) override
    {
        return canned;
    }
};

// Counts handler invocations and returns a configurable result.
struct HandlerRecorder
{
    int calls = 0;
    CommandResult canned;

    HandlerRecorder()
    {
        canned.result_json = R"({"ok":true})";
        canned.audit_target = "created-slug";
    }

    CommandHandler handler()
    {
        return [this](CommandContext&, const ArgsView&) -> CommandResult {
            ++calls;
            return canned;
        };
    }
};

// Shares a stack-owned sink with a registry without transferring ownership.
std::shared_ptr<IAuditSink> alias(CapturingAuditSink& sink)
{
    return std::shared_ptr<IAuditSink>(std::shared_ptr<void>(), &sink);
}

std::shared_ptr<IJobSubmitter> alias(FakeJobSubmitter& submitter)
{
    return std::shared_ptr<IJobSubmitter>(std::shared_ptr<void>(), &submitter);
}

// A caller with a recognizable principal/transport.
CallerContext test_caller(ControlSet role)
{
    CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = "test";
    caller.control_set = role;
    return caller;
}

// A sync descriptor; mutating iff the permission is not read.
CommandDescriptor sync_descriptor(std::string name, Permission permission,
                                  CommandHandler handler)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "a summary";
    descriptor.permission = permission;
    descriptor.mutating = permission != Permission::read;
    descriptor.handler = std::move(handler);
    return descriptor;
}

// A mutating job descriptor whose factory returns a trivial body.
CommandDescriptor job_descriptor(std::string name)
{
    CommandDescriptor descriptor;
    descriptor.name = std::move(name);
    descriptor.summary = "a summary";
    descriptor.permission = Permission::write;
    descriptor.mutating = true;
    descriptor.kind = ExecutionKind::job;
    descriptor.job_factory =
        [](const CommandContext&, const ArgsView&)
        -> std::variant<JobBody, CommandError> {
        return JobBody {
            [](IJobContext&) { return JobOutcome {}; }};
    };
    return descriptor;
}

CommandRequest request_for(std::string command, std::string args_json = "{}")
{
    CommandRequest request;
    request.id = "client-1";
    request.command = std::move(command);
    request.args_json = std::move(args_json);
    return request;
}

// Concatenates every string field of a record, for leakage assertions.
std::string flatten(const AuditRecord& record)
{
    return record.principal + '\n' + record.transport + '\n' + record.command +
           '\n' + record.target + '\n' + record.outcome;
}

}  // namespace

// ---------------------------------------------------------------------------
// Mutating success: exactly one record, outcome "ok", target from the
// handler's audit_target, principal/transport from the caller.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(mutating_success_emits_one_ok_record)
{
    HandlerRecorder recorder;
    CapturingAuditSink sink;
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));
    registry.add(
        sync_descriptor("link.delete", Permission::write, recorder.handler()));

    const CommandResponse response = registry.execute(
        test_caller(ControlSet::Admin), request_for("link.delete"));

    BOOST_TEST(response.ok == true);
    BOOST_TEST(recorder.calls == 1);
    BOOST_REQUIRE(sink.records.size() == 1u);
    BOOST_TEST(sink.records[0].outcome == "ok");
    BOOST_TEST(sink.records[0].target == "created-slug");
    BOOST_TEST(sink.records[0].command == "link.delete");
    BOOST_TEST(sink.records[0].principal == "local-operator");
    BOOST_TEST(sink.records[0].transport == "test");
}

// ---------------------------------------------------------------------------
// Denial: User calling a write command yields one not_authorized record and
// the handler is never entered.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(denied_emits_one_not_authorized_record)
{
    HandlerRecorder recorder;
    CapturingAuditSink sink;
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));
    registry.add(
        sync_descriptor("link.delete", Permission::write, recorder.handler()));

    const CommandResponse response = registry.execute(
        test_caller(ControlSet::User), request_for("link.delete"));

    BOOST_TEST(response.ok == false);
    BOOST_TEST(recorder.calls == 0);
    BOOST_REQUIRE(sink.records.size() == 1u);
    BOOST_TEST(sink.records[0].outcome == "not_authorized");
    BOOST_TEST(sink.records[0].target.empty());
}

// ---------------------------------------------------------------------------
// Invalid args: one invalid_field record, handler not entered.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(invalid_args_emits_one_invalid_field_record)
{
    HandlerRecorder recorder;
    CapturingAuditSink sink;
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));

    CommandDescriptor descriptor =
        sync_descriptor("link.delete", Permission::write, recorder.handler());
    ArgSpec slug;
    slug.name = "slug";
    slug.type = ArgType::string;
    slug.required = true;
    descriptor.args = {slug};
    registry.add(std::move(descriptor));

    const CommandResponse response = registry.execute(
        test_caller(ControlSet::Admin), request_for("link.delete", R"({})"));

    BOOST_TEST(response.ok == false);
    BOOST_TEST(recorder.calls == 0);
    BOOST_REQUIRE(sink.records.size() == 1u);
    BOOST_TEST(sink.records[0].outcome == "invalid_field");
    BOOST_TEST(sink.records[0].target.empty());
}

// ---------------------------------------------------------------------------
// Dry run of a supports_dry_run sync command -> outcome "dry_run".
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(dry_run_emits_dry_run_outcome)
{
    HandlerRecorder recorder;
    CapturingAuditSink sink;
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));

    CommandDescriptor descriptor =
        sync_descriptor("link.delete", Permission::write, recorder.handler());
    descriptor.supports_dry_run = true;
    registry.add(std::move(descriptor));

    CommandRequest request = request_for("link.delete");
    request.options.dry_run = true;
    const CommandResponse response =
        registry.execute(test_caller(ControlSet::Admin), request);

    BOOST_TEST(response.ok == true);
    BOOST_REQUIRE(sink.records.size() == 1u);
    BOOST_TEST(sink.records[0].outcome == "dry_run");
    BOOST_TEST(sink.records[0].target == "created-slug");
}

// ---------------------------------------------------------------------------
// Accepted job -> "job_accepted", target = the accepted job id.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(accepted_job_emits_job_accepted_with_job_id)
{
    CapturingAuditSink sink;
    FakeJobSubmitter submitter;
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));
    registry.set_job_submitter(alias(submitter));
    registry.add(job_descriptor("link.export"));

    const CommandResponse response = registry.execute(
        test_caller(ControlSet::Admin), request_for("link.export"));

    BOOST_TEST(response.ok == true);
    BOOST_REQUIRE(sink.records.size() == 1u);
    BOOST_TEST(sink.records[0].outcome == "job_accepted");
    BOOST_TEST(sink.records[0].target == "j-7");
}

// ---------------------------------------------------------------------------
// Submitter rejects with busy -> outcome "busy".
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(busy_submitter_emits_busy_outcome)
{
    CapturingAuditSink sink;
    FakeJobSubmitter submitter;
    submitter.canned.job.reset();
    submitter.canned.error = make_error(CommandErrorCode::busy, "queue full");
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));
    registry.set_job_submitter(alias(submitter));
    registry.add(job_descriptor("link.export"));

    const CommandResponse response = registry.execute(
        test_caller(ControlSet::Admin), request_for("link.export"));

    BOOST_TEST(response.ok == false);
    BOOST_REQUIRE(sink.records.size() == 1u);
    BOOST_TEST(sink.records[0].outcome == "busy");
}

// ---------------------------------------------------------------------------
// Handler exception -> outcome "internal".
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(throwing_handler_emits_internal_outcome)
{
    CapturingAuditSink sink;
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));
    registry.add(sync_descriptor(
        "link.delete", Permission::write,
        [](CommandContext&, const ArgsView&) -> CommandResult {
            throw std::runtime_error("secret-dsn");
        }));

    const CommandResponse response = registry.execute(
        test_caller(ControlSet::Admin), request_for("link.delete"));

    BOOST_TEST(response.ok == false);
    BOOST_REQUIRE(sink.records.size() == 1u);
    BOOST_TEST(sink.records[0].outcome == "internal");
}

// ---------------------------------------------------------------------------
// Read-only command and unknown command emit zero records.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(read_only_and_unknown_emit_nothing)
{
    HandlerRecorder recorder;
    CapturingAuditSink sink;
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));
    registry.add(
        sync_descriptor("link.get", Permission::read, recorder.handler()));

    const CommandResponse read_resp = registry.execute(
        test_caller(ControlSet::Admin), request_for("link.get"));
    BOOST_TEST(read_resp.ok == true);

    const CommandResponse unknown_resp = registry.execute(
        test_caller(ControlSet::Admin), request_for("ns.absent"));
    BOOST_TEST(unknown_resp.ok == false);

    BOOST_TEST(sink.records.empty());
}

// ---------------------------------------------------------------------------
// No value leakage (C6): a secret arg value and a token-looking slug value are
// present in the request, yet appear in no captured record field.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(no_argument_value_leaks_into_record)
{
    CapturingAuditSink sink;
    CommandRegistry registry;
    registry.set_audit_sink(alias(sink));

    CommandDescriptor descriptor = sync_descriptor(
        "link.delete", Permission::write,
        [](CommandContext&, const ArgsView&) -> CommandResult {
            CommandResult result;
            result.result_json = R"({"ok":true})";
            // The handler sets a validated, value-free target.
            result.audit_target = "safe-slug";
            return result;
        });
    ArgSpec password;
    password.name = "password";
    password.type = ArgType::string;
    ArgSpec slug;
    slug.name = "slug";
    slug.type = ArgType::string;
    descriptor.args = {password, slug};
    registry.add(std::move(descriptor));

    const CommandResponse response = registry.execute(
        test_caller(ControlSet::Admin),
        request_for(
            "link.delete",
            R"({"password":"hunter2-SECRET","slug":"tok_LEAKTOKEN_x"})"));

    BOOST_TEST(response.ok == true);
    BOOST_REQUIRE(sink.records.size() == 1u);
    const std::string flat = flatten(sink.records[0]);
    BOOST_TEST(flat.find("hunter2-SECRET") == std::string::npos);
    BOOST_TEST(flat.find("LEAKTOKEN") == std::string::npos);
    BOOST_TEST(sink.records[0].target == "safe-slug");
}

// ---------------------------------------------------------------------------
// audit_fields: fixed keys, 300-char principal truncated to <=128, newline and
// ESC control characters sanitized to '?'.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(audit_fields_keys_truncation_and_control_chars)
{
    AuditRecord record;
    record.principal = std::string(300, 'p');
    record.transport = "line1\nline2\x1b[0m";
    record.command = "link.delete";
    record.target = "slug";
    record.outcome = "ok";

    const std::vector<LogField> fields = audit_fields(record);

    BOOST_REQUIRE(fields.size() == 6u);
    BOOST_TEST(fields[0].key == "event");
    BOOST_TEST(fields[0].value == "command_audit");
    BOOST_TEST(fields[1].key == "principal");
    BOOST_TEST(fields[2].key == "transport");
    BOOST_TEST(fields[3].key == "command");
    BOOST_TEST(fields[4].key == "target");
    BOOST_TEST(fields[5].key == "outcome");

    BOOST_TEST(fields[1].value.size() <= 128u);
    BOOST_TEST(fields[2].value.find('\n') == std::string::npos);
    BOOST_TEST(fields[2].value.find('\x1b') == std::string::npos);
    BOOST_TEST(fields[2].value.find('?') != std::string::npos);
}

// ---------------------------------------------------------------------------
// T-03: a bare 0x80 byte and an embedded NUL are neutralized; no raw invalid
// UTF-8 or NUL byte survives into the serialized fields.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(audit_fields_neutralizes_invalid_utf8_and_nul)
{
    AuditRecord record;
    record.principal = std::string("bad\x80here");
    record.transport = std::string("has\0nul", 7);
    record.command = "link.delete";
    record.target = "slug";
    record.outcome = "ok";

    const std::vector<LogField> fields = audit_fields(record);

    BOOST_TEST(fields[1].value.find('\x80') == std::string::npos);
    BOOST_TEST(fields[1].value.find('?') != std::string::npos);
    BOOST_TEST(fields[2].value.find('\0') == std::string::npos);
    BOOST_TEST(fields[2].value.find('?') != std::string::npos);

    // No field carries a raw NUL or a bare 0x80 continuation byte.
    for (const LogField& field : fields)
    {
        BOOST_TEST(field.value.find('\0') == std::string::npos);
        BOOST_TEST(field.value.find(static_cast<char>(0x80)) ==
                   std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// No sink set: a mutating command runs normally. Installing a sink enables
// emission; clearing it with null disables emission again.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(null_sink_disables_emission)
{
    HandlerRecorder recorder;
    CommandRegistry registry;
    registry.add(
        sync_descriptor("link.delete", Permission::write, recorder.handler()));

    // No sink: the mutating command still runs and succeeds.
    const CommandResponse no_sink = registry.execute(
        test_caller(ControlSet::Admin), request_for("link.delete"));
    BOOST_TEST(no_sink.ok == true);

    // Install a sink: one record for the next execution.
    CapturingAuditSink sink;
    registry.set_audit_sink(alias(sink));
    registry.execute(test_caller(ControlSet::Admin),
                     request_for("link.delete"));
    BOOST_TEST(sink.records.size() == 1u);

    // Clear the sink with null: no further records.
    registry.set_audit_sink(nullptr);
    registry.execute(test_caller(ControlSet::Admin),
                     request_for("link.delete"));
    BOOST_TEST(sink.records.size() == 1u);
}

// ---------------------------------------------------------------------------
// LoggerAuditSink smoke: record() does not throw for an empty record nor for an
// oversized/hostile one.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(logger_audit_sink_record_never_throws)
{
    LoggerAuditSink sink {Logger("audit")};

    AuditRecord empty;
    BOOST_CHECK_NO_THROW(sink.record(empty));

    AuditRecord oversized;
    oversized.principal = std::string(5000, 'x');
    oversized.transport = std::string("ctl\x01\x80\0bytes", 10);
    oversized.command = std::string(5000, 'c');
    oversized.target = std::string(5000, 't');
    oversized.outcome = "internal";
    BOOST_CHECK_NO_THROW(sink.record(oversized));
}

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "url_shortener/commands/command_registry.hpp"

#include <openssl/sha.h>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/audit.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/commands/job_submitter.hpp"
#include "url_shortener/commands/json_scan.hpp"
#include "url_shortener/core/utils.hpp"
#include "url_shortener/security/access_guard.hpp"

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Checks whether a caller's role satisfies a required permission,
 * without leaking the outcome as an exception.
 *
 * Builds an @ref AccessGuard from `caller.control_set` and invokes the
 * `require*` method matching @p permission; the guard throws
 * @ref AccessDeniedError on denial, which this helper catches and reports as
 * `false`. This is the single place that maps a @ref Permission onto the
 * 1:1 `ControlSet` capability queries (`read`->`requireRead`, `write`->
 * `requireWrite`, `migrate`->`requireMigration`, `manage_users`->
 * `requireUserManagement`); no other command-layer code performs an
 * authorization check (milestone contract C7).
 *
 * @param caller The authenticated caller; only `control_set` is consulted.
 * @param permission The permission the command requires.
 * @return `true` when the caller's `control_set` holds @p permission; `false`
 * when the guard denies it. Never throws for a denial.
 */
bool permission_check(const CallerContext& caller, Permission permission)
{
    const AccessGuard guard(caller.control_set);
    try {
        // Each case grants only after its require* call returns. There is
        // deliberately no `default`: -Wswitch still flags a new enumerator,
        // and any unmapped value falls through to the fail-closed return.
        switch (permission) {
            case Permission::read:
                guard.requireRead();
                return true;
            case Permission::write:
                guard.requireWrite();
                return true;
            case Permission::migrate:
                guard.requireMigration();
                return true;
            case Permission::manage_users:
                guard.requireUserManagement();
                return true;
        }
    } catch (const AccessDeniedError&) {
        return false;
    }
    return false;
}

/**
 * @brief How `execute` must dispatch a request once authorization and argument
 * validation have passed, given the request options and the descriptor.
 *
 * The one place that encodes the subtask-08 options/job table so the dispatch
 * step is a single switch. `error` means the requested mode is `unsupported`
 * for this descriptor; the other values select the sync handler (plain or
 * dry_run) or the job path (synchronous dry_run body, or real submission).
 */
enum class dispatch
{
    ///< Run the sync handler normally with `ctx.dry_run == false`.
    handler,

    ///< Run the sync handler with `ctx.dry_run == true` (the command is
    ///< `supports_dry_run` and not async); it must not mutate.
    dry_run,

    ///< Build the job body via `job_factory` with `ctx.dry_run == true` and run
    ///< it synchronously on the caller thread with a no-op context; no job is
    ///< created.
    dry_run_job,

    ///< Build the job body via `job_factory` and hand it to the installed
    ///< submitter (`kind == job`, or `async` on an `async_capable` command).
    job,

    ///< The requested mode is not supported for this descriptor; map to
    ///< `unsupported` with the reason written to the out-param.
    error
};

/**
 * @brief Resolves the dispatch mode from the request options and descriptor
 * (the subtask-08 step-5 table).
 *
 * Pure decision logic; it consults no submitter and performs no I/O, so the
 * no-submitter fail-closed check lives at the call site, not here.
 *
 * @param descriptor The resolved descriptor.
 * @param options The request's parsed mode flags.
 * @param[out] unsupported_reason When non-null and the result is
 * @ref dispatch::error, set to a fixed, value-free reason (`"dry_run not
 * supported"`, `"dry_run and async are mutually exclusive"`, or `"async not
 * supported"`); left unchanged for every other result.
 * @return The dispatch mode: `error` when the mode is unsupported, else
 * `handler`/`dry_run`/`dry_run_job`/`job` per the table.
 */
dispatch resolve_dispatch(const CommandDescriptor& descriptor,
                          const CommandOptions& options,
                          std::string* unsupported_reason)
{
    const auto fail = [&](const char* reason)
    {
        if (unsupported_reason != nullptr) {
            *unsupported_reason = reason;
        }
        return dispatch::error;
    };

    if (options.dry_run) {
        if (!descriptor.supports_dry_run) {
            return fail("dry_run not supported");
        }
        if (options.async) {
            // dry_run validates-and-authorizes only; a job would mutate.
            return fail("dry_run and async are mutually exclusive");
        }
        return descriptor.kind == ExecutionKind::job ? dispatch::dry_run_job
                                                     : dispatch::dry_run;
    }

    // Not a dry run. A job command always runs as a job; a sync command runs
    // as a job only when async was requested and the command is async_capable.
    if (descriptor.kind == ExecutionKind::job) {
        return dispatch::job;
    }
    if (options.async) {
        return descriptor.async_capable ? dispatch::job
                                        : fail("async not supported");
    }
    return dispatch::handler;
}

/**
 * @brief Computes the `args_digest` that travels with a submitted job: the
 * first 16 hex characters of `SHA-256(args_json)`.
 *
 * Only this digest (never the raw `args_json`) is handed to the submitter, so
 * secrets passed as arguments are never persisted in the job record (threat
 * model T-13). Equal args always yield an equal digest.
 *
 * @param args_json The exact `args` object source text.
 * @return A 16-character lower-case hex string.
 */
std::string args_digest(std::string_view args_json)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(args_json.data()),
           args_json.size(),
           digest);
    std::string hex = url_shortener::hexEncode(digest, SHA256_DIGEST_LENGTH);
    hex.resize(16);
    return hex;
}

/**
 * @brief A no-op @ref IJobContext used to run a `dry_run` job body
 * synchronously on the caller thread.
 *
 * Reports no cancellation and discards progress: a dry_run never cancels and
 * nothing consumes its progress. A `supports_dry_run` job body (Task 03.0) must
 * perform no mutation and only bounded work under dry_run (threat model T-21).
 */
class NoopJobContext final : public IJobContext
{
public:
    /**
     * @brief Always reports that cancellation has not been requested.
     * @return `false`, always.
     */
    bool cancel_requested() const noexcept override { return false; }

    /**
     * @brief Discards the declared unit total.
     * @param total Ignored.
     */
    void set_total(std::uint64_t total) override { (void)total; }

    /**
     * @brief Discards a progress advance.
     * @param n Ignored.
     */
    void advance(std::uint64_t n = 1) override { (void)n; }
};

/**
 * @brief Converts a @ref JobOutcome into a response for the synchronous
 * `dry_run` of a `job` command.
 *
 * @param id Correlation id to echo; moved into the response.
 * @param outcome The outcome the dry_run body produced on the caller thread.
 * @return @ref make_failure with `outcome.failure` when the body failed,
 * `job_cancelled` when it reported cancellation (defensive; the no-op context
 * never requests it), `internal` when a successful body produced no payload,
 * else @ref make_success with the body's `result_json`.
 */
CommandResponse dry_run_job_response(std::string id, JobOutcome outcome)
{
    if (outcome.cancelled) {
        return make_failure(
            std::move(id),
            make_error(CommandErrorCode::job_cancelled, "cancelled"));
    }
    if (!outcome.ok) {
        return make_failure(std::move(id), std::move(outcome.failure));
    }
    if (!outcome.result_json || outcome.result_json->empty()) {
        return make_failure(
            std::move(id),
            make_error(CommandErrorCode::internal, "internal error"));
    }
    return make_success(std::move(id), std::move(*outcome.result_json));
}

/**
 * @brief Runs a `supports_dry_run` job command's body synchronously on the
 * caller thread, creating no job (subtask-08 step-5, threat model T-21).
 *
 * Builds the body through `descriptor.job_factory` with `ctx.dry_run == true`,
 * then runs it with a @ref NoopJobContext. The factory, and the body it
 * returns, run inside `catch (const std::exception&)`/`catch (...)`, so an
 * escaping exception maps to `internal` with no `what()` leaked (threat model
 * T-10). A factory @ref CommandError becomes a failure envelope.
 *
 * @param descriptor The resolved `kind == job`, `supports_dry_run` descriptor.
 * @param caller The authenticated caller; referenced only for the duration of
 * the factory/body call.
 * @param request The parsed request; its `id` is echoed and its `args_json`
 * is passed to the factory via the view.
 * @param view The validated, non-owning argument view.
 * @return A success response carrying the body's result, or a failure response
 * (factory error, body failure, or `internal`).
 */
CommandResponse run_dry_run_job(const CommandDescriptor& descriptor,
                                const CallerContext& caller,
                                const CommandRequest& request,
                                const ArgsView& view)
{
    try {
        const CommandContext ctx {caller, request, true};
        std::variant<JobBody, CommandError> produced =
            descriptor.job_factory(ctx, view);
        if (std::holds_alternative<CommandError>(produced)) {
            return make_failure(request.id,
                                std::get<CommandError>(std::move(produced)));
        }
        JobBody body = std::get<JobBody>(std::move(produced));
        NoopJobContext job_ctx;
        JobOutcome outcome = body(job_ctx);
        return dry_run_job_response(request.id, std::move(outcome));
    } catch (const std::exception&) {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::internal, "internal error"));
    } catch (...) {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::internal, "internal error"));
    }
}

/**
 * @brief Builds a job body and submits it through the installed submitter
 * (subtask-08 step-5, threat model T-12/T-13).
 *
 * With no submitter installed the request fails closed with `unsupported` and
 * `descriptor.job_factory` is never called. Otherwise the factory builds the
 * body (with `ctx.dry_run == false`); a factory @ref CommandError becomes a
 * failure envelope with nothing queued, and a @ref JobBody is handed to
 * @ref IJobSubmitter::submit together with the command name and the
 * `args_digest` (never raw args). The factory and the submitter run inside
 * `catch (const std::exception&)`/`catch (...)`, so an escaping exception maps
 * to `internal` with no `what()` leaked (threat model T-10).
 *
 * @param descriptor The resolved descriptor whose body is to be submitted.
 * @param caller The authenticated caller; referenced only during the factory
 * call.
 * @param request The parsed request; its `id` is echoed, `command` and
 * `args_digest(args_json)` travel with the job.
 * @param view The validated, non-owning argument view.
 * @param submitter The installed submitter, or `nullptr` for the fail-closed
 * path.
 * @return @ref make_job_accepted on acceptance; a failure response for a null
 * submitter (`unsupported`), a factory error, a submitter rejection
 * (`busy`/`internal`/...), or an escaping exception (`internal`).
 */
CommandResponse submit_job(const CommandDescriptor& descriptor,
                           const CallerContext& caller,
                           const CommandRequest& request,
                           const ArgsView& view,
                           const std::shared_ptr<IJobSubmitter>& submitter)
{
    // Fail closed before touching the factory: no runtime, no job (T-12).
    if (!submitter) {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::unsupported, "jobs not supported"));
    }

    try {
        const CommandContext ctx {caller, request, false};
        std::variant<JobBody, CommandError> produced =
            descriptor.job_factory(ctx, view);
        if (std::holds_alternative<CommandError>(produced)) {
            // A factory error queues nothing.
            return make_failure(request.id,
                                std::get<CommandError>(std::move(produced)));
        }
        JobBody body = std::get<JobBody>(std::move(produced));
        JobSubmitResult submitted = submitter->submit(
            request.command, args_digest(request.args_json), std::move(body));
        if (submitted.job.has_value()) {
            return make_job_accepted(request.id, std::move(*submitted.job));
        }
        // "Either a job or an error, never both": no job means a real error.
        return make_failure(request.id, std::move(submitted.error));
    } catch (const std::exception&) {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::internal, "internal error"));
    } catch (...) {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::internal, "internal error"));
    }
}

/**
 * @brief Derives the audit `outcome` string from a finished response and the
 * resolved dry_run flag (subtask-09 emission rules).
 *
 * The single place that maps a @ref CommandResponse to the one-word outcome an
 * @ref AuditRecord carries: a failure yields the error code's wire name
 * (`not_authorized`, `invalid_field`, `unsupported`, `busy`, `internal`, ...);
 * an accepted job yields `job_accepted`; any other success yields `dry_run`
 * when the run was a dry run, else `ok`.
 *
 * @param response The response `execute` is about to return.
 * @param dry_run `true` when the execution ran under `options.dry_run` (a sync
 * `supports_dry_run` handler or a synchronous dry_run job body).
 * @return The outcome word for the audit record.
 */
std::string derive_audit_outcome(const CommandResponse& response, bool dry_run)
{
    if (!response.ok) {
        return std::string(error_info_for(response.error.code).name);
    }
    if (response.job.has_value()) {
        return "job_accepted";
    }
    return dry_run ? "dry_run" : "ok";
}

/**
 * @brief Checks that every `default_value` in an argument list (including
 * nested `object` fields) is exactly one well-formed JSON value.
 *
 * Uses the same technique as the response serializer: the token is scanned as
 * the sole member of a synthetic `{"r":<token>}` object, which rejects
 * malformed tokens and trailing data such as `1},"x":{`.
 *
 * @param args The argument specs to check; recursion depth is bounded by the
 * descriptor author's static `fields` nesting, not by input.
 * @return `true` when every present default is a single JSON value.
 */
bool defaults_are_single_json_values(const std::vector<ArgSpec>& args)
{
    for (const ArgSpec& arg : args) {
        if (arg.default_value.has_value()) {
            const std::string wrapped = "{\"r\":" + *arg.default_value + "}";
            const scan_result scan = scan_object(wrapped);
            if (!scan.ok || scan.members.size() != 1) {
                return false;
            }
        }
        if (!defaults_are_single_json_values(arg.fields)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool CommandRegistry::is_valid_descriptor(const CommandDescriptor& descriptor,
                                          std::string* why)
{
    // Writes a fixed reason into `why` (when provided) and returns false; the
    // reasons are fixed developer-facing text, never caller-supplied values.
    const auto fail = [&](const char* reason)
    {
        if (why != nullptr) {
            *why = reason;
        }
        return false;
    };

    if (!is_valid_command_name(descriptor.name)) {
        return fail("invalid command name");
    }
    if (descriptor.summary.empty()) {
        return fail("empty summary");
    }
    if (descriptor.kind == ExecutionKind::sync && !descriptor.handler) {
        return fail("null handler for sync command");
    }
    if ((descriptor.kind == ExecutionKind::job || descriptor.async_capable)
        && !descriptor.job_factory)
    {
        return fail("missing job_factory");
    }

    std::set<std::string_view> seen_arg_names;
    for (const ArgSpec& arg : descriptor.args) {
        if (!seen_arg_names.insert(arg.name).second) {
            return fail("duplicate arg name");
        }
    }

    // command.list embeds each default verbatim; a malformed token would turn
    // the whole catalog response into an internal error at run time, so
    // reject it here at startup instead.
    if (!defaults_are_single_json_values(descriptor.args)) {
        return fail("invalid arg default");
    }

    if (descriptor.supports_dry_run && !descriptor.mutating) {
        return fail("supports_dry_run requires mutating");
    }

    // A state-changing command must not be runnable with read permission
    // alone; the descriptor is the only authorization source (C7). A
    // permission_for hook may still raise a read base per call (D16).
    if (descriptor.mutating && descriptor.permission == Permission::read
        && !descriptor.permission_for)
    {
        return fail("mutating requires write permission");
    }

    if (why != nullptr) {
        why->clear();
    }
    return true;
}

void CommandRegistry::add(CommandDescriptor descriptor)
{
    std::string why;
    if (!is_valid_descriptor(descriptor, &why)) {
        throw std::invalid_argument(why);
    }

    // Copy the key out before moving the descriptor: argument evaluation order
    // within emplace is unspecified, so reading descriptor.name after a move
    // from descriptor would be unsafe.
    std::string key = descriptor.name;
    const bool inserted =
        m_commands.emplace(std::move(key), std::move(descriptor)).second;
    if (!inserted) {
        throw std::invalid_argument("duplicate command name");
    }
}

const CommandDescriptor* CommandRegistry::find(std::string_view name) const
{
    const auto it = m_commands.find(name);
    return it == m_commands.end() ? nullptr : &it->second;
}

std::vector<const CommandDescriptor*> CommandRegistry::list() const
{
    std::vector<const CommandDescriptor*> result;
    result.reserve(m_commands.size());
    for (const auto& entry : m_commands) {
        result.push_back(&entry.second);
    }
    return result;
}

void CommandRegistry::set_job_submitter(
    std::shared_ptr<IJobSubmitter> submitter)
{
    // Startup-only, not synchronized; replaces any previous submitter. A null
    // argument clears the seam, restoring the fail-closed `unsupported` path.
    m_job_submitter_ = std::move(submitter);
}

void CommandRegistry::set_audit_sink(std::shared_ptr<IAuditSink> sink)
{
    // Startup-only, not synchronized; replaces any previous sink. A null
    // argument clears the seam, disabling emission.
    m_audit = std::move(sink);
}

void CommandRegistry::emit_audit(const CommandDescriptor& descriptor,
                                 const CallerContext& caller,
                                 std::string outcome,
                                 std::string target) const
{
    if (!m_audit) {
        return;
    }

    AuditRecord record;
    record.principal = caller.principal;
    record.transport = caller.transport;
    record.command = descriptor.name;
    // Cap the target at the AuditRecord bound (64); audit_fields later
    // neutralizes any byte a cap might have split (threat model T-15).
    if (target.size() > 64) {
        target.resize(64);
    }
    record.target = std::move(target);
    record.outcome = std::move(outcome);

    // The sink is noexcept by contract; wrap anyway so a throwing fake can
    // never alter the response (threat model T-14).
    try {
        m_audit->record(record);
    } catch (...) {
    }
}

CommandResponse CommandRegistry::execute(const CallerContext& caller,
                                         const CommandRequest& request) const
{
    // Step 1: envelope sanity (defense in depth; parse_request normally
    // catches these). No input value is echoed.
    if (request.v != 1 || request.command.empty()) {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::invalid_envelope, "invalid envelope"));
    }

    // Step 2: lookup. The unknown name is deliberately not echoed.
    const CommandDescriptor* descriptor = find(request.command);
    if (descriptor == nullptr) {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::unknown_command, "unknown command"));
    }

    // From here on the command has a descriptor, so a mutating command must
    // produce exactly one audit record. Steps 3-7 run inside `pipeline` so
    // every exit funnels through the single `emit_audit` call below (one record
    // per attempt, threat model T-14). `audit_dry_run`/`audit_target` capture
    // the two facts the response alone cannot carry (whether the run was a
    // dry_run, and the handler's validated target).
    bool audit_dry_run = false;
    std::string audit_target;

    const auto pipeline = [&]() -> CommandResponse
    {
        // Step 3: base permission, BEFORE argument validation, so an
        // unauthorized caller learns nothing about the arg schema and the
        // handler is never entered on denial (threat model T-08).
        if (!permission_check(caller, descriptor->permission)) {
            return make_failure(request.id,
                                make_error(CommandErrorCode::not_authorized,
                                           "permission denied"));
        }

        // Step 4: argument validation. The message names the arg and a fixed
        // reason, never an argument value (contract C6).
        ArgsView view;
        CommandError arg_error;
        if (!validate_args(
                descriptor->args, request.args_json, &view, &arg_error))
        {
            return make_failure(request.id, std::move(arg_error));
        }

        // Step 4a: argument-dependent permission re-check (decision log D16).
        // The hook runs only on the validated view; the effective permission is
        // max(base, hook) over the contractual order, so it can only tighten
        // (threat model T-09). A throwing hook maps to internal.
        if (descriptor->permission_for) {
            Permission required = descriptor->permission;
            try {
                const Permission hinted = descriptor->permission_for(view);
                required = std::max(descriptor->permission, hinted);
            } catch (const std::exception&) {
                return make_failure(
                    request.id,
                    make_error(CommandErrorCode::internal, "internal error"));
            } catch (...) {
                return make_failure(
                    request.id,
                    make_error(CommandErrorCode::internal, "internal error"));
            }

            if (!permission_check(caller, required)) {
                return make_failure(request.id,
                                    make_error(CommandErrorCode::not_authorized,
                                               "permission denied"));
            }
        }

        // Step 5: options/job handling (subtask 08). resolve_dispatch encodes
        // the whole options table; the handler cases fall through to step 6 and
        // the job cases return here. The job/dry_run paths run only after steps
        // 3-4a, so an unauthorized or invalid request never reaches the factory
        // (T-12).
        bool handler_dry_run = false;
        std::string unsupported_reason;
        switch (
            resolve_dispatch(*descriptor, request.options, &unsupported_reason))
        {
            case dispatch::handler:
                break;
            case dispatch::dry_run:
                handler_dry_run = true;
                break;
            case dispatch::dry_run_job:
                audit_dry_run = true;
                return run_dry_run_job(*descriptor, caller, request, view);
            case dispatch::job:
                return submit_job(
                    *descriptor, caller, request, view, m_job_submitter_);
            case dispatch::error:
                return make_failure(request.id,
                                    make_error(CommandErrorCode::unsupported,
                                               std::move(unsupported_reason)));
        }

        // Step 6: dispatch with exception isolation. e.what() is never
        // surfaced: it may carry paths, SQL or a DSN (threat model T-10).
        audit_dry_run = handler_dry_run;
        CommandResult result;
        try {
            CommandContext ctx {caller, request, handler_dry_run};
            result = descriptor->handler(ctx, view);
        } catch (const std::exception&) {
            return make_failure(
                request.id,
                make_error(CommandErrorCode::internal, "internal error"));
        } catch (...) {
            return make_failure(
                request.id,
                make_error(CommandErrorCode::internal, "internal error"));
        }

        // The target is recorded for both success and expected failure; it is a
        // validated slug/id the handler set, never an argument value (C6).
        audit_target = result.audit_target;

        // Step 7: convert the handler outcome into a response.
        if (!result.ok()) {
            return make_failure(request.id, std::move(result.error));
        }
        if (result.result_json.empty()) {
            // A successful handler must produce a payload; an empty one is a
            // handler programming error, reported as internal.
            return make_failure(
                request.id,
                make_error(CommandErrorCode::internal, "internal error"));
        }
        return make_success(request.id, std::move(result.result_json));
    };

    CommandResponse response = pipeline();

    // Step 8: exactly one audit record per attempt at a mutating command
    // (subtask 09, threat model T-14). Non-mutating commands emit nothing; an
    // accepted job records its job id, every other outcome its derived word.
    if (descriptor->mutating) {
        // Building the record allocates; under memory exhaustion that must
        // neither escape execute (documented never-throwing) nor change the
        // response the handler already produced.
        try {
            std::string target =
                response.job.has_value() ? response.job->id : audit_target;
            emit_audit(*descriptor,
                       caller,
                       derive_audit_outcome(response, audit_dry_run),
                       std::move(target));
        } catch (...) {
            // Audit is best-effort under allocation failure; the response
            // stands.
        }
    }

    return response;
}

CommandResponse CommandRegistry::execute_json(
    const CallerContext& caller,
    std::string_view request_text,
    const envelope_limits& limits) const
{
    parse_request_result parsed = parse_request(request_text, limits);
    if (!parsed.ok()) {
        // The parser already resolved a safe echo id (valid client id or a
        // srv-<n> fallback, threat model T-05) and a value-free envelope error.
        return make_failure(std::move(parsed.id), std::move(parsed.error));
    }
    return execute(caller, *parsed.request);
}

}  // namespace url_shortener::commands

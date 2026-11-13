#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/envelope.hpp>

namespace url_shortener::commands
{

class IJobSubmitter;

/**
 * @brief The in-process catalog of command descriptors and the one entry point
 * that executes a command (milestone contracts C1/C7/C11).
 *
 * A registry is populated once at startup (every process mode builds its own
 * via `composition::build_command_runtime`; there is no global instance) and is
 * then read-only. Because of that single-phase lifecycle, @ref find, @ref list
 * and @ref execute take no lock and are safe to call concurrently after
 * registration completes; calling @ref add after serving has started is a
 * programming error and is **not** synchronized.
 *
 * Authorization: @ref execute trusts @ref CallerContext::control_set as the
 * sole authorization root (milestone contract C7, threat model T-11). The
 * registry applies no transport-based restriction; the D4 guarantees
 * (command API off by default, loopback-only `admin`, `manage_users` never over
 * REST) are the responsibility of the Task 04.0 adapters, not this class.
 *
 * Non-copyable because registered handlers and factories may capture `this`;
 * copying would leave the copies' callables pointing at the original.
 */
class CommandRegistry
{
public:
    /**
     * @brief Constructs an empty registry.
     */
    CommandRegistry() = default;

    /**
     * @brief Deleted copy constructor; handlers/factories may capture `this`.
     */
    CommandRegistry(const CommandRegistry&) = delete;

    /**
     * @brief Deleted copy assignment; handlers/factories may capture `this`.
     */
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    /**
     * @brief Registers one command descriptor, validating it first.
     *
     * Intended for startup only. Validity is a programming-error gate, so a bad
     * descriptor throws rather than returning an error (CLAUDE.md: exceptions
     * are reserved for programming errors and unrecoverable startup failures).
     *
     * @param descriptor The descriptor to register; moved into the registry on
     * success and consumed on either outcome.
     * @throws std::invalid_argument when the descriptor is inconsistent: a name
     * that is not a valid dotted command name (@ref is_valid_command_name); an
     * empty `summary`; a null `handler` on a `sync` command; a missing
     * `job_factory` on a `job` or `async_capable` command; a duplicate argument
     * name within `args`; `supports_dry_run` set without `mutating`; a
     * `mutating` command with `read` base permission and no `permission_for`
     * hook; a `supports_dry_run` job command without a `handler`; or a `name`
     * already present in this registry.
     */
    void add(CommandDescriptor descriptor);

    /**
     * @brief Looks up a descriptor by exact command name.
     *
     * Uses heterogeneous lookup, so no temporary `std::string` is allocated.
     *
     * @param name The exact command name to find.
     * @return A pointer to the registered descriptor, or `nullptr` when no
     * command has that name (including the empty name). The pointer stays valid
     * for the lifetime of the registry even across later @ref add calls, because
     * the backing `std::map` has node-stable iterators/references.
     */
    const CommandDescriptor* find(std::string_view name) const;

    /**
     * @brief Lists every registered descriptor in ascending name order.
     *
     * The deterministic order (the backing `std::map`'s key order) makes the
     * generated catalog and `command.list` output stable across calls and runs.
     *
     * @return A vector of non-owning pointers to the registered descriptors,
     * sorted by name; each pointer is valid for the lifetime of the registry.
     */
    std::vector<const CommandDescriptor*> list() const;

    /**
     * @brief Executes one parsed command on behalf of a caller: the single
     * entry point every transport uses (milestone contracts C1/C7, ADR 0002
     * section 2).
     *
     * The pipeline runs in this fixed order; each step either produces a
     * failure response and stops, or falls through to the next:
     *
     * 1. Envelope sanity (defense in depth; @ref parse_request normally catches
     *    it): `request.v != 1` or an empty `command` -> `invalid_envelope`.
     * 2. Lookup: @ref find on `request.command`; a miss -> `unknown_command`
     *    with the fixed message `unknown command` (the submitted name is never
     *    echoed).
     * 3. Base permission: an `AccessGuard` built from `caller.control_set`
     *    enforces `descriptor.permission` (`read`->`requireRead`, `write`->
     *    `requireWrite`, `migrate`->`requireMigration`, `manage_users`->
     *    `requireUserManagement`); denial -> `not_authorized` with the fixed
     *    message `permission denied`. This precedes argument validation so an
     *    unauthorized caller learns nothing about the arg schema, and the
     *    handler is never entered on denial (threat model T-08).
     * 4. Argument validation: @ref validate_args against `descriptor.args`; a
     *    violation -> `invalid_field` with the `"<arg>: <reason>"` message from
     *    subtask 05 (never an argument value).
     * 4a. Argument-dependent permission (decision log D16, threat model T-09):
     *    when `descriptor.permission_for` is set it is evaluated on the
     *    validated view and the effective permission is
     *    `max(descriptor.permission, permission_for(view))` on the contractual
     *    order `read < write < migrate < manage_users`; a failing re-check ->
     *    `not_authorized` (same fixed message). The `max` makes the hook
     *    raise-only. A hook that throws -> `internal`.
     * 5. Options/job handling (subtask 08), resolved from `request.options` and
     *    the descriptor:
     *    - `dry_run` on a `!supports_dry_run` descriptor, or `dry_run` together
     *      with `async` (mutually exclusive), -> `unsupported`.
     *    - `async` on a `sync` descriptor that is not `async_capable` ->
     *      `unsupported`.
     *    - `dry_run` on a `supports_dry_run` sync descriptor: the handler runs
     *      (step 6) with `ctx.dry_run == true` and must not mutate.
     *    - `dry_run` on a `supports_dry_run` `job` descriptor: `job_factory`
     *      runs with `ctx.dry_run == true` and the returned body runs
     *      **synchronously** on the caller thread with a no-op @ref IJobContext;
     *      its @ref JobOutcome becomes the result. No job is created.
     *    - `async` on an `async_capable` descriptor, or any `kind == job`
     *      descriptor (not under `dry_run`): with no submitter installed ->
     *      `unsupported` (fail closed, `job_factory` not called); otherwise
     *      `job_factory` runs, a `CommandError` -> failure envelope with nothing
     *      queued, and a @ref JobBody is handed to the installed
     *      @ref IJobSubmitter whose @ref JobSubmitResult yields either
     *      @ref make_job_accepted or a failure (`busy`/`internal`/...).
     *    `job_factory`, @ref IJobSubmitter::submit and a `dry_run` job body each
     *    run inside `catch (const std::exception&)` and `catch (...)`; an
     *    escaping exception -> `internal` (same isolation as the handler; its
     *    `what()` is never surfaced, threat model T-10). Only an `args_digest`
     *    (never raw args) travels with a submitted job (threat model T-13).
     * 6. Dispatch: `descriptor.handler(ctx, view)` runs inside
     *    `catch (const std::exception&)` and `catch (...)`; any escaping
     *    exception -> `internal` with the fixed message `internal error`. The
     *    exception's `what()` is never surfaced: it may carry paths, SQL or a
     *    DSN (threat model T-10).
     * 7. Conversion: a handler error -> @ref make_failure with that error; a
     *    success with empty `result_json` -> `internal` (a handler programming
     *    error); otherwise @ref make_success with the handler's `result_json`
     *    verbatim.
     *
     * Authorization is performed here and only here (contract C7); adapters
     * must not re-implement it. `caller.control_set` is trusted as the sole
     * authorization root (threat model T-11). `const` and never throws; because
     * the registry is read-only after startup, concurrent calls are safe.
     * Thread-safety of a handler's own work is the handler's concern.
     *
     * @param caller The authenticated caller; `control_set` is the sole
     * authorization input, the string fields are diagnostic only.
     * @param request The parsed, envelope-validated request to execute; its
     * `id` is echoed in every response.
     * @return A success response (handler `result_json` embedded verbatim) or a
     * failure response echoing `request.id`, with one of `invalid_envelope`,
     * `unknown_command`, `not_authorized`, `invalid_field`, `unsupported`,
     * `internal`, or the handler's own error code.
     */
    CommandResponse execute(const CallerContext& caller,
                            const CommandRequest& request) const;

    /**
     * @brief Parses a request-envelope JSON text and executes it: the
     * convenience entry point for the REST generic endpoint, stdio and the CLI
     * `cmd` adapter.
     *
     * Runs @ref parse_request on @p request_text and, on success, forwards the
     * parsed request to @ref execute. A parse failure becomes a failure
     * response carrying the parser's `invalid_envelope` error and the
     * best-effort echo id from @ref parse_request (a valid client `id` or a
     * `srv-<n>` fallback, never raw hostile input; threat model T-05). The
     * caller serializes the returned response with @ref serialize_response.
     *
     * @param caller The authenticated caller; see @ref execute.
     * @param request_text Untrusted request-envelope JSON; never logged here.
     * @param limits Byte/id-length/command-length/depth caps for the parse;
     * defaults mirror the server's request-hardening limits.
     * @return The execution response on a successful parse, or a failure
     * response with `error.code == invalid_envelope` and the best-effort echo
     * id on a parse failure. `const` and never throws.
     */
    CommandResponse execute_json(const CallerContext& caller,
                                 std::string_view request_text,
                                 const envelope_limits& limits = {}) const;

    /**
     * @brief Installs the one process-wide job submitter the registry hands
     * accepted job bodies to (decision log D13).
     *
     * Startup-only configuration, mirroring @ref add: it replaces any previous
     * submitter, is **not** thread-safe, and must be called before serving
     * starts. The registry keeps a `std::shared_ptr`, so a per-request registry
     * (REST, Task 04.0) can share the process-wide runner without owning its
     * lifetime. There is no second mechanism (no `set_job_runner`): without a
     * submitter, every job or `async` request fails closed with `unsupported`
     * and no @ref JobHandlerFactory is invoked (threat model T-12).
     *
     * @param submitter The submitter to install, typically Task 05.0's
     * `JobRunner`; shared ownership is taken. A null pointer clears the seam,
     * restoring the fail-closed `unsupported` behavior.
     */
    void set_job_submitter(std::shared_ptr<IJobSubmitter> submitter);

private:
    /**
     * @brief Checks that a descriptor is internally consistent, independent of
     * what is already registered.
     *
     * Enforces every intrinsic registration rule (name shape, non-empty
     * summary, handler/factory presence by kind, unique argument names,
     * `supports_dry_run` implies `mutating`, `mutating` implies write-or-higher
     * permission unless a raise hook is present, a dry_run job has a handler);
     * the duplicate-name rule is checked by @ref add against the current
     * contents, not here.
     *
     * @param descriptor The candidate descriptor.
     * @param[out] why When non-null, set to a fixed reason string on failure and
     * cleared on success; left as the fixed reason on the `false` return so
     * @ref add can throw it.
     * @return `true` when the descriptor passes every intrinsic rule; `false`
     * on the first rule violated (invalid name, empty summary, null handler on a
     * sync command, missing job_factory on a job/async_capable command,
     * duplicate argument name, `supports_dry_run` without `mutating`, a
     * mutating command with read permission and no raise hook, or a dry_run
     * job command without a handler).
     */
    static bool is_valid_descriptor(const CommandDescriptor& descriptor,
                                    std::string* why);

    ///< The registered descriptors keyed by name. `std::less<>` enables
    ///< heterogeneous `std::string_view` lookup; the node-based container keeps
    ///< element pointers stable across inserts, which @ref find relies on.
    std::map<std::string, CommandDescriptor, std::less<>> m_commands;

    ///< The installed job submitter, or `nullptr` when none is set (the
    ///< fail-closed default). Shared, not owned exclusively, so a per-request
    ///< registry can point at the one process-wide runner. Set by
    ///< @ref set_job_submitter before serving; read by @ref execute.
    std::shared_ptr<IJobSubmitter> m_job_submitter_;
};

}  // namespace url_shortener::commands

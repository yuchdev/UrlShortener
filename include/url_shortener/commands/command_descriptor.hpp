#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/security/control_set.hpp>

namespace url_shortener::commands
{

/**
 * @brief The permission a command requires of its caller's role.
 *
 * This is command-layer authorization data carried on a @ref CommandDescriptor
 * (milestone contract C7). The four values form a strictly increasing severity
 * order used by `execute` (subtask 07) when combining the descriptor's base
 * permission with an optional @ref PermissionHook:
 * `read < write < migrate < manage_users`. The registry maps each value to a
 * capability query on `ControlSet` (`canRead`/`canWrite`/`canMigrate`/
 * `canManageUsers`) rather than comparing roles directly. Enumerator order is
 * contractual: it defines that severity order.
 */
enum class Permission
{
    ///< Read-only access; granted to every `ControlSet` (`canRead`). The
    ///< lowest severity.
    read,

    ///< A mutating action; granted only to roles with `canWrite` (`Admin`).
    write,

    ///< A schema/migration action; granted only to roles with `canMigrate`
    ///< (`Admin`). Higher severity than `write`.
    migrate,

    ///< A user-management action; granted only to roles with `canManageUsers`
    ///< (`Admin`). The highest severity; no such command exists in this
    ///< milestone (reserved for Task 0002).
    manage_users
};

/**
 * @brief How a command is dispatched: inline on the caller thread, or as a job.
 *
 * Selects which descriptor field `execute` uses. `sync` commands run through
 * @ref CommandDescriptor::handler and return an immediate result; `job`
 * commands build a @ref JobBody through @ref CommandDescriptor::job_factory and
 * are handed to the job submitter (subtask 08). A `job` command is not
 * necessarily mutating (e.g. `link.export` is a read job). Enumerator order is
 * not contractual.
 */
enum class ExecutionKind
{
    ///< Runs inline on the caller thread via @ref CommandDescriptor::handler
    ///< and yields an immediate @ref CommandResult.
    sync,

    ///< Prepared by @ref CommandDescriptor::job_factory and executed
    ///< asynchronously by the job submitter; the handler, when present, serves
    ///< the `dry_run` path only.
    job
};

/**
 * @brief The authenticated caller on whose behalf a command runs, supplied by
 * the transport adapter.
 *
 * Security: `execute` trusts `control_set` as the **sole** authorization root
 * (milestone contract C7, threat model T-11); the registry performs no
 * transport-based restriction of its own. The D4 guarantees (command API off by
 * default, loopback-only `admin`, `manage_users` never over REST) are enforced
 * by the Task 04.0 adapters that build this struct, not here. A buggy or
 * malicious adapter that sets `control_set = Admin` for a remote peer therefore
 * gets full access; defense in depth lives in the adapter layer. The string
 * fields are identity/diagnostic labels only and never influence authorization.
 */
struct CallerContext
{
    ///< Human/service identity for audit lines, e.g. `"local-operator"`, a
    ///< session user, or `"stdio-parent"`. Diagnostic only; not an
    ///< authorization input.
    std::string principal;

    ///< The caller's role, the only input `execute` authorizes against. The
    ///< adapter is fully trusted to set this correctly (threat model T-11).
    ControlSet control_set = ControlSet::User;

    ///< Originating transport label for audit/diagnostics, e.g. `"rest"`,
    ///< `"cli"`, `"tui"`, `"stdio"`, `"test"`. Not an authorization input.
    std::string transport;
};

/**
 * @brief The per-invocation context handed to a command handler.
 *
 * An aggregate of non-owning references plus the resolved `dry_run` flag.
 * Lifetime: `caller` and `request` are references into objects owned by
 * `execute`'s caller; both must outlive this context. A handler (or a job
 * factory) that needs any of their data after it returns must **copy** it; it
 * must never store the references or the context itself.
 */
struct CommandContext
{
    ///< The authenticated caller; must outlive this context. Not owned.
    const CallerContext& caller;

    ///< The parsed, envelope-validated request being executed; must outlive
    ///< this context. Not owned. Its `args_json` is validated against the
    ///< descriptor's @ref ArgSpec set before the handler sees it.
    const CommandRequest& request;

    ///< `true` when the caller requested a validate-and-authorize-only run;
    ///< a handler for a `supports_dry_run` command must then perform no
    ///< mutation and only bounded work.
    bool dry_run = false;
};

/**
 * @brief The outcome of a synchronous command handler.
 *
 * Exactly one of two states is meaningful: on success `error.ok()` is `true`
 * and `result_json` holds the handler's payload; on an expected failure `error`
 * carries the category and a value-free message and `result_json` is empty.
 * `audit_target` is independent of success and feeds the audit line (subtask
 * 09). Unexpected failures are not represented here: a handler that throws is
 * mapped to `internal` by `execute` (subtask 07), never surfaced as a
 * `CommandResult`.
 */
struct CommandResult
{
    ///< The handler's success payload as verbatim JSON text, embedded into the
    ///< response envelope unchanged (milestone contract C1). Must be built with
    ///< the trusted serializers, never by concatenating untrusted input (threat
    ///< model T-17). Empty on failure.
    std::string result_json;

    ///< The failure detail for an expected failure (`not_found`,
    ///< `slug_conflict`, ...); `code == none` on success. Its message carries
    ///< no argument value (milestone contract C6).
    CommandError error;

    ///< The slug or id recorded in the audit line for a mutating command;
    ///< never an argument value and capped by the audit sink (subtask 09).
    ///< May be empty when there is no meaningful target.
    std::string audit_target;

    /**
     * @brief Indicates whether the handler succeeded.
     * @return `true` when `error.code == CommandErrorCode::none`; `false`
     * otherwise.
     */
    bool ok() const { return error.ok(); }
};

/**
 * @brief A synchronous command handler: the one implementation of a command
 * (milestone contract C1).
 *
 * Invoked by `execute` only after the request has been authorized and its args
 * validated against the descriptor's @ref ArgSpec set. The @ref CommandContext
 * is passed by non-const reference so a handler may read `dry_run`; the
 * validated @ref ArgsView is non-owning and transient (see @ref ArgsView), so a
 * handler must copy any value it needs to retain. A handler reports expected
 * failures through the returned @ref CommandResult and must not throw for them;
 * an escaping exception is mapped to `internal` by `execute` (subtask 07).
 */
using CommandHandler = std::function<CommandResult(CommandContext&,
                                                   const ArgsView&)>;

/**
 * @brief The capabilities a running job body may use, implemented by the job
 * runtime (Task 05.0 subtask 02) and declared here so the descriptor can carry
 * a @ref JobHandlerFactory.
 *
 * A @ref JobBody receives one of these to report progress and observe
 * cancellation. The registry itself neither implements nor stores an
 * `IJobContext`; it hands the body to an `IJobSubmitter` (subtask 08).
 */
class IJobContext
{
public:
    /**
     * @brief Virtual destructor; bodies interact through a base pointer.
     */
    virtual ~IJobContext() = default;

    /**
     * @brief Tests whether cancellation has been requested for this job.
     * @return `true` once `job.cancel` has asked the body to stop; a
     * cooperative body should finish early and report `cancelled`. `noexcept`.
     */
    virtual bool cancel_requested() const noexcept = 0;

    /**
     * @brief Declares the total unit count for progress reporting.
     * @param total The expected number of units of work; may be called once
     * before advancing.
     */
    virtual void set_total(std::uint64_t total) = 0;

    /**
     * @brief Advances the completed-unit counter for progress reporting.
     * @param n Number of units just completed; defaults to one.
     */
    virtual void advance(std::uint64_t n = 1) = 0;
};

/**
 * @brief The terminal outcome of a running @ref JobBody.
 *
 * Reports which of the three terminal job states the body reached. `ok == true`
 * with `cancelled == false` is success; `cancelled == true` means the body
 * stopped early after observing @ref IJobContext::cancel_requested; `ok ==
 * false` with `cancelled == false` is a failure that must carry @ref failure.
 */
struct JobOutcome
{
    ///< `true` when the body completed its work successfully.
    bool ok = true;

    ///< `true` when the body stopped early because cancellation was requested;
    ///< a partial `result_json` may still be set.
    bool cancelled = false;

    ///< The body's result payload as verbatim JSON text; may be set for a
    ///< successful, failed, or cancelled (partial) outcome, or left unset.
    std::optional<std::string> result_json;

    ///< The failure detail; required when `!ok && !cancelled`, ignored
    ///< otherwise. Its message carries no argument value (contract C6).
    CommandError failure;
};

/**
 * @brief A self-contained unit of asynchronous work produced by a
 * @ref JobHandlerFactory.
 *
 * Runs on a job worker thread with an @ref IJobContext for progress and
 * cancellation. The body must own every value it operates on (the factory
 * deep-copied them), so it stays valid after the originating request buffer is
 * freed (threat model T-12).
 */
using JobBody = std::function<JobOutcome(IJobContext&)>;

/**
 * @brief Prepares a @ref JobBody on the caller thread, or reports why it
 * cannot (decision log D13).
 *
 * Runs after authorization and @ref ArgSpec validation (and @ref
 * PermissionHook) have passed, on the caller thread, so it may do bounded
 * preparation such as opening files. It returns **either** a ready @ref JobBody
 * **or** a @ref CommandError, never both.
 *
 * Lifetime: the @ref ArgsView and @ref CommandContext passed in are non-owning
 * and transient; the factory MUST copy every value the body needs into the
 * returned body so the body holds no reference to request-owned memory (threat
 * model T-12). Required on a descriptor whose `kind == job` or that is
 * `async_capable`.
 */
using JobHandlerFactory = std::function<std::variant<JobBody, CommandError>(
    const CommandContext&, const ArgsView&)>;

/**
 * @brief Optional argument-dependent permission hook (decision log D16).
 *
 * Evaluated by `execute` (subtask 07) only after @ref ArgSpec validation
 * succeeds, on a validated @ref ArgsView. It is **raise-only**: the effective
 * permission is `max(descriptor base, hook result)` on the order
 * `read < write < migrate < manage_users`, so a hook can never lower the
 * descriptor's base permission (threat model T-09). Example: `link.export`'s
 * base is `read`, and the hook raises it to `write` when a `path` argument
 * directs the export to a file. The hook must be pure and must not throw; an
 * escaping exception is mapped to `internal` by `execute`.
 */
using PermissionHook = std::function<Permission(const ArgsView&)>;

/**
 * @brief The single definition of one external operation: its schema,
 * authorization, dispatch mode and implementation (milestone contracts C1/C7).
 *
 * Every transport (REST, CLI, TUI, stdio, future IPC) executes a command
 * through this one descriptor and its one handler; no transport carries
 * per-command logic. A descriptor is registered once at startup via
 * @ref CommandRegistry::add, which rejects an inconsistent descriptor (see
 * that method). The only cross-field rule is that `supports_dry_run` requires
 * `mutating`; `kind == job` does not imply `mutating`.
 */
struct CommandDescriptor
{
    ///< Dotted command name, e.g. `"link.create"`; must satisfy
    ///< @ref is_valid_command_name and be unique in its registry.
    std::string name;

    ///< One-line human summary for the generated catalog and help; must be
    ///< non-empty.
    std::string summary;

    ///< The argument schema for this command; member names must be unique.
    ///< Drives validation, CLI flag compilation and TUI form generation.
    std::vector<ArgSpec> args;

    ///< Base permission required of the caller, checked by `execute` before
    ///< @ref ArgSpec validation. May only be raised by @ref permission_for,
    ///< never lowered (threat model T-09).
    Permission permission = Permission::read;

    ///< Optional hook that raises @ref permission based on validated args
    ///< (decision log D16); empty when the permission is argument-independent.
    PermissionHook permission_for;

    ///< Whether the command runs inline (`sync`) or as a job.
    ExecutionKind kind = ExecutionKind::sync;

    ///< `true` when the command changes state; drives TUI confirmation and the
    ///< one-per-execution audit line (subtask 09). Independent of `kind`.
    bool mutating = false;

    ///< `true` when `options.dry_run` is honored; requires `mutating` (the one
    ///< cross-field registration rule).
    bool supports_dry_run = false;

    ///< `true` when `options.async` may run this command as a job; requires a
    ///< @ref job_factory even if `kind == sync`.
    bool async_capable = false;

    ///< Documentation-only mapping to equivalent surfaces, e.g.
    ///< `"POST /api/v1/links"`, `"link create"`. Consumed by the catalog
    ///< renderer only; never used for routing.
    std::vector<std::string> maps_to;

    ///< The synchronous handler; required when `kind == sync`. For a `job`
    ///< command it is optional and, when present, serves the `dry_run` path.
    CommandHandler handler;

    ///< The job-body factory; required when `kind == job` or `async_capable`,
    ///< and unused otherwise (decision log D13).
    JobHandlerFactory job_factory;
};

}  // namespace url_shortener::commands

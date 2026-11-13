#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/envelope.hpp>

namespace url_shortener::commands
{

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
     * name within `args`; `supports_dry_run` set without `mutating`; or a
     * `name` already present in this registry.
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
     * @brief Executes a command on behalf of a caller (subtask 07).
     *
     * This subtask ships a stub only: it performs no lookup, authorization,
     * validation or dispatch and always reports an `internal` failure. The full
     * pipeline (authorize via `AccessGuard`, validate args, raise-only
     * `permission_for`, sync/dry_run/job dispatch, audit) is implemented in
     * subtask 07 and replaces this body.
     *
     * @param caller The authenticated caller; trusted as the sole authorization
     * root once the stub is replaced (threat model T-11).
     * @param request The parsed, envelope-validated request to execute; its
     * `id` is echoed in the response.
     * @return Until subtask 07 lands, always a failure response echoing
     * `request.id` with `error.code == internal`.
     */
    CommandResponse execute(const CallerContext& caller,
                            const CommandRequest& request) const;

private:
    /**
     * @brief Checks that a descriptor is internally consistent, independent of
     * what is already registered.
     *
     * Enforces every intrinsic registration rule (name shape, non-empty
     * summary, handler/factory presence by kind, unique argument names,
     * `supports_dry_run` implies `mutating`); the duplicate-name rule is checked
     * by @ref add against the current contents, not here.
     *
     * @param descriptor The candidate descriptor.
     * @param[out] why When non-null, set to a fixed reason string on failure and
     * cleared on success; left as the fixed reason on the `false` return so
     * @ref add can throw it.
     * @return `true` when the descriptor passes every intrinsic rule; `false`
     * on the first rule violated (invalid name, empty summary, null handler on a
     * sync command, missing job_factory on a job/async_capable command,
     * duplicate argument name, or `supports_dry_run` without `mutating`).
     */
    static bool is_valid_descriptor(const CommandDescriptor& descriptor,
                                    std::string* why);

    ///< The registered descriptors keyed by name. `std::less<>` enables
    ///< heterogeneous `std::string_view` lookup; the node-based container keeps
    ///< element pointers stable across inserts, which @ref find relies on.
    std::map<std::string, CommandDescriptor, std::less<>> m_commands;
};

}  // namespace url_shortener::commands

#pragma once

#include <vector>

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/command_client.hpp>

namespace url_shortener::tui
{

/**
 * @brief Reconciles a response envelope with its own `ok`/payload invariant
 * (threat model T-113).
 *
 * Checks that the three mutually exclusive payloads and the `ok` flag agree:
 * a success (`ok == true`) carries exactly one of a `result` or a `job` and no
 * `error`; a failure (`ok == false`) carries an `error` and no `result`/`job`.
 * Any violation - `ok:true` with an `error`, `ok:false` with a result/job,
 * both a success payload and an `error`, or neither - is treated as a
 * malformed/ambiguous response and replaced so it cannot render as a false
 * success. This is the `ok`/`result`/`error` agreement guard only; tightening
 * the lenient `commands::parse_response` (unknown top-level keys) stays with
 * the Task 01.0 owner.
 *
 * @param response The response to check; moved in.
 * @return @p response unchanged when it is self-consistent; otherwise a fresh
 * `internal` failure envelope echoing @p response's `id` with a fixed,
 * value-free message.
 */
commands::CommandResponse reconcile_envelope(
    commands::CommandResponse response);

/**
 * @brief The default @ref ICommandClient: runs commands directly on an
 * in-process `commands::CommandRegistry` (ADR 0003 Decision 2, contract C8).
 *
 * This is the only TUI type that sees a `CommandRegistry`; every screen depends
 * on @ref ICommandClient instead. @ref call forwards to
 * `CommandRegistry::execute` verbatim, wraps it against a thrown exception so a
 * handler bug cannot kill the UI, and reconciles the envelope (@ref
 * reconcile_envelope, T-113) before returning.
 *
 * Lifetime: the client holds a non-owning reference to @p registry, which MUST
 * outlive the client; it copies @p caller. It constructs neither the registry
 * nor the runtime - `tui_entry.cpp` (subtask 19) builds those via
 * `composition::build_command_runtime` over the active backend and hands the
 * registry here.
 *
 * Thread-safety: `CommandRegistry` is read-only after startup and its
 * `find`/`list`/`execute` are `const` and safe to call concurrently, so a
 * single `InProcessClient` may be shared by the UI thread (for @ref commands /
 * @ref session) and a worker thread (for @ref call) without external
 * synchronization; the client adds no mutable state of its own.
 */
class InProcessClient final : public ICommandClient
{
public:
    /**
     * @brief Constructs a client bound to a registry and a caller identity.
     *
     * @param registry The command registry to execute against; a non-owning
     * reference that MUST outlive this client.
     * @param caller The authenticated caller every command runs as; copied.
     * Its `control_set` is the registry's sole authorization input (C7) and
     * its `principal`/`transport` label @ref session.
     */
    InProcessClient(const commands::CommandRegistry& registry,
                    commands::CallerContext caller);

    /**
     * @brief Executes @p request on the bound registry (D12).
     *
     * Forwards to `CommandRegistry::execute(caller, request)` unchanged; the
     * registry already renders business failures as `error` envelopes. A
     * `commands::CommandRegistry::execute` never throws, but as defense in
     * depth any escaping exception is caught and turned into an `internal`
     * error envelope. The result is then run through @ref reconcile_envelope
     * (T-113). Blocking; call off the UI thread.
     *
     * @param request The command to run; its `id` is echoed.
     * @return The registry's response, or an `internal` error envelope on an
     * unexpected exception or a self-contradictory envelope.
     */
    commands::CommandResponse call(
        const commands::CommandRequest& request) override;

    /**
     * @brief Lists every registered command as @ref CommandInfo.
     *
     * Maps each descriptor from `CommandRegistry::list` (ascending by name):
     * `ArgType` to @ref ArgKind, the base `Permission` to its wire string,
     * and `kind`/`mutating`/`supports_dry_run` and each argument's
     * `nullable`/`enum_values`/object `fields` across.
     *
     * @return One @ref CommandInfo per registered descriptor, name-sorted.
     */
    std::vector<CommandInfo> commands() const override;

    /**
     * @brief Returns the bound caller's session view.
     *
     * @return A @ref SessionInfo with `principal`/`transport` from the caller
     * and `read_only == true` exactly when the caller cannot write (runs as
     * `ControlSet::User`).
     */
    SessionInfo session() const override;

private:
    ///< The registry commands execute against; not owned, must outlive this
    ///< client (see the class lifetime contract).
    const commands::CommandRegistry& m_registry;

    ///< The authenticated caller every command runs as; its `control_set` is
    ///< the sole authorization input (C7). Copied at construction.
    commands::CallerContext m_caller;
};

}  // namespace url_shortener::tui

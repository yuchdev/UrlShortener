#pragma once

#include <functional>
#include <memory>
#include <string>

#include <ftxui/component/component_base.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/capability.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/toast.hpp>

namespace url_shortener::tui
{

/**
 * @file mutation_flow.hpp
 * @brief The single choke point every mutating TUI submit passes through
 * (ADR 0003 Decision 2 rule 4, subtask 06.0/08): @ref MutationFlow decides the
 * confirmation with @ref ConfirmPolicy, routes a required confirm through the
 * D10 dialog, submits off the UI thread with @ref AsyncCaller, and turns the
 * response into a @ref Toast. The forms of 08 and the bespoke screens of 11-16
 * reuse it, so "mutations confirm" is enforced in exactly one place.
 *
 * Belongs to `url_shortener_tui` (it renders the confirm dialog, so it links
 * FTXUI). The flow never builds JSON (the @ref FormModel does, contract C1) and
 * never names an FTXUI colour (it styles only through the injected @ref Theme).
 */

/**
 * @brief The collaborators a @ref MutationFlow needs, injected by reference.
 *
 * A thin, non-owning aggregate: every member is a reference the caller owns and
 * must keep alive for the whole lifetime of the @ref MutationFlow (and of every
 * completion still in flight). It intentionally does not carry the screen's
 * `on_success` / navigation callbacks; those stay with the caller, which passes
 * a per-submission `on_result` into @ref MutationFlow::begin.
 */
struct MutationDeps
{
    ///< Runs the command off the UI thread and posts the response back. Must
    ///< outlive the flow and any in-flight completion (T-120).
    AsyncCaller& caller;

    ///< Decides the confirmation (none / yes-no / type-token) for a command.
    ///< Must outlive the flow.
    ConfirmPolicy& policy;

    ///< Receives the success / error / job toast the flow derives from each
    ///< response. Must outlive the flow.
    ToastQueue& toasts;

    ///< Styles the confirm dialog's disabled button (mono-safe). Must outlive
    ///< the flow.
    const Theme& theme;

    ///< The single capability decision (subtask 18). In a read-only session a
    ///< request whose resolved effective permission is not `read` (the model's
    ///< resolver honors argument-dependent raises, D16) is refused in
    ///< @ref MutationFlow::begin with a toast and zero submits, so no write
    ///< leaves the TUI even through a direct @ref run_mutation caller (threat
    ///< model T-116, defence in depth over the registry's own `AccessGuard`).
    ///< Built by the screen from @ref capability_for; moved in.
    CapabilityModel capability;
};

/**
 * @brief Drives one mutating (or read) submission from policy decision to
 * toast.
 *
 * Call @ref begin (or the free @ref run_mutation) with a built request and its
 * @ref CommandInfo. For a @ref ConfirmKind::none decision (every non-mutating
 * command, and every command in a `--yes` session) the request is submitted
 * immediately; otherwise the flow opens a confirm dialog, and only a confirmed
 * dialog submits (a cancelled one sends nothing - success criterion 4). The
 * dialog is rendered and driven through the always-present @ref overlay
 * component the owning screen layers over its body.
 *
 * Threading: @ref begin and @ref overlay run on the UI thread; the actual
 * command runs on an @ref AsyncCaller worker and its response handler is posted
 * back to the UI thread, so the flow never blocks (ADR 0003 rule 3).
 */
class MutationFlow
{
public:
    /**
     * @brief Builds the flow over its collaborators.
     *
     * @param deps The injected caller/policy/toasts/theme; every member is a
     * reference captured for the flow's lifetime (see @ref MutationDeps).
     */
    explicit MutationFlow(MutationDeps deps);

    /**
     * @brief Cancels any in-flight submission so a late completion is dropped.
     *
     * Suppresses the outstanding submit through @ref AsyncCaller::cancel (its
     * handler never fires) and clears the shared alive guard checked by the
     * posted completion, then runs the base destructor. This is the UAF fix
     * (threat model T-120, CWE-416): a @ref MutationFlow owned by a screen that
     * is destroyed mid-session (an inline Link detail / edit form) can no
     * longer run @ref handle_response against freed memory, because the posted
     * completion holds a `weak_ptr` to this guard and no-ops once it expires.
     * Safe for every host: the destructor runs while the injected
     * @ref AsyncCaller is still alive (@ref App destroys every screen, hence
     * every flow, before the caller), so the @ref cancel call never touches a
     * dead caller.
     */
    ~MutationFlow();

    MutationFlow(const MutationFlow&) = delete;
    MutationFlow& operator=(const MutationFlow&) = delete;
    MutationFlow(MutationFlow&&) = delete;
    MutationFlow& operator=(MutationFlow&&) = delete;

    /**
     * @brief Drops any pending confirm and suppresses any in-flight submit.
     *
     * A host calls this from its `on_leave` / cancel path when it is about to
     * be hidden or torn down: the open confirm dialog is discarded (no submit)
     * and the outstanding call is cancelled so its toast / `on_result` never
     * fires. Idempotent and safe to call when nothing is pending. It does not
     * clear the alive guard, so the flow may be reused afterwards (e.g. a
     * refresh).
     */
    void cancel();

    /**
     * @brief The choke point: decide the confirmation, then submit now or after
     * a confirm (ADR 0003 rule 4).
     *
     * When @ref MutationDeps::read_only is set and @p request's resolved
     * effective permission (via @ref AsyncCaller::required_permission, so an
     * argument-dependent raise is honored, D16) is not `read`, the submission
     * is refused up front: a `not_authorized` toast is shown and nothing
     * reaches the caller (threat model T-116). Otherwise it consults @ref
     * ConfirmPolicy::decide with @p info and @p request's `args_json`. On @ref
     * ConfirmKind::none it submits @p request straight away; otherwise it opens
     * the confirm dialog and defers the submit until the operator confirms (a
     * cancel discards the submission with zero calls). A new @ref begin while a
     * confirm is still open or a call is still in flight replaces the pending
     * submission.
     *
     * @param request The fully-built request to submit; its `id` is echoed
     * back. Moved in.
     * @param info The command being invoked; drives the confirmation decision
     * and the toast's source-command label. Only its fields are read; not
     * retained.
     * @param on_result Invoked on the UI thread with the response once the
     * submission completes (never when the confirm is cancelled); moved in. May
     * be empty.
     */
    void begin(commands::CommandRequest request,
               const CommandInfo& info,
               std::function<void(const commands::CommandResponse&)> on_result);

    /**
     * @brief Whether a confirm dialog is currently open and awaiting a
     * decision.
     *
     * @return `true` between a confirming @ref begin and the operator's
     * confirm/cancel; `false` otherwise (including while a submitted call is in
     * flight).
     */
    bool awaiting_confirm() const { return m_awaiting_; }

    /**
     * @brief Whether a submitted call is still outstanding.
     *
     * Reflects the injected @ref AsyncCaller's GLOBAL in-flight count, not this
     * flow's own token: `busy()` is `true` whenever *any* command submitted
     * through the same caller (any screen, including a background refresh) is
     * still running, even if this particular flow has submitted nothing. This
     * is deliberately conservative - it is used only to refuse a duplicate
     * submit (a double Ctrl-S sends one request), so erring toward "busy" at
     * worst makes the operator press again once the shared caller drains; it
     * never drops or misattributes a request.
     *
     * @return `true` when the injected @ref AsyncCaller reports any work in
     * flight; `false` when the caller is idle.
     */
    bool busy() const;

    /**
     * @brief The always-present overlay component the screen layers over its
     * body.
     *
     * Renders nothing and consumes no events while @ref awaiting_confirm is
     * `false`; while a confirm is open it renders the D10 dialog and routes
     * keys to it, submitting the deferred request when the dialog is confirmed.
     * The returned component is stable for the flow's lifetime (safe to place
     * once).
     *
     * @return The overlay component; never null.
     */
    ftxui::Component overlay() { return m_overlay_; }

    /**
     * @brief Renders the overlay element for the current state (internal,
     * called by the overlay component).
     * @return An empty element when no confirm is open; otherwise the D10
     * dialog.
     */
    ftxui::Element render_overlay() const;

    /**
     * @brief Routes one event to the open confirm dialog (internal, called by
     * the overlay component).
     *
     * A no-op returning `false` when no confirm is open; otherwise delegates to
     * the dialog and, once the dialog decides, submits (on confirm) or discards
     * (on cancel) the deferred request.
     *
     * @param event The event to route.
     * @return `true` when a confirm is open (the overlay is modal while shown).
     */
    bool overlay_event(const ftxui::Event& event);

private:
    /// Submits @ref m_request_ through the caller and wires the toast handler.
    void submit_now();

    /// Pushes the success / error / job toast for @p resp, then calls
    /// @ref m_on_result_.
    void handle_response(const commands::CommandResponse& resp);

    ///< Injected collaborators (caller/policy/toasts/theme).
    MutationDeps m_deps_;

    ///< The open confirm dialog's state; null when no confirm is open.
    std::unique_ptr<ConfirmModel> m_confirm_;

    ///< The confirm dialog component bound to @ref m_confirm_; rebuilt per
    ///< confirming @ref begin.
    ftxui::Component m_dialog_;

    ///< The stable overlay component returned by @ref overlay.
    ftxui::Component m_overlay_;

    ///< `true` while a confirm dialog is open and awaiting a decision.
    bool m_awaiting_ = false;

    ///< The request deferred behind an open confirm, or the one just submitted.
    commands::CommandRequest m_request_;

    ///< The invoked command's name, for the derived toast's source label.
    std::string m_command_name_;

    ///< The per-submission result handler supplied to @ref begin.
    std::function<void(const commands::CommandResponse&)> m_on_result_;

    ///< Token of the in-flight submit; `0` when none is outstanding. Cancelled
    ///< by @ref cancel and the destructor so a late completion is suppressed.
    CallToken m_token_ = 0;

    ///< Owned alive guard shared (as a `weak_ptr`) with every posted
    ///< completion; cleared when this flow is destroyed so the completion
    ///< no-ops instead of running @ref handle_response against freed memory
    ///< (threat model T-120, CWE-416). A `shared_ptr` so the completion can
    ///< safely outlive the flow.
    std::shared_ptr<bool> m_alive_ = std::make_shared<bool>(true);
};

/**
 * @brief Free-function spelling of the mutation choke point (the name 11-16
 * call).
 *
 * Equivalent to `flow.begin(std::move(request), info, std::move(on_result))`;
 * provided so every mutating submit reads as one `run_mutation(...)` call
 * (ADR 0003 rule 4). A read command passed here is simply submitted directly
 * (its policy decision is @ref ConfirmKind::none), so routing every submit
 * through this one function is safe.
 *
 * @param flow The flow that owns the confirm/submit state; must outlive any
 * in-flight completion.
 * @param request The request to submit; moved in.
 * @param info The command being invoked; read only.
 * @param on_result The UI-thread result handler; moved in. May be empty.
 */
void run_mutation(
    MutationFlow& flow,
    commands::CommandRequest request,
    const CommandInfo& info,
    std::function<void(const commands::CommandResponse&)> on_result);

}  // namespace url_shortener::tui

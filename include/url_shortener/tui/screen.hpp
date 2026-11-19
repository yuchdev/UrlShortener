#pragma once

#include <string>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/capability.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/router.hpp>
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/tui_options.hpp>

namespace url_shortener::tui
{

/**
 * @brief Everything a screen is allowed to touch (ADR 0003 rule 1, contract
 * C8): the async client facade, the session, the router and shared UI options.
 *
 * Deliberately narrow - a screen reaches the application only through
 * @ref caller (which wraps @ref ICommandClient) and never sees a registry,
 * service, repository or DTO. Later subtasks widen it (a `const Theme&` by 05,
 * a `ToastQueue&` by 06); the cross-screen hand-off fields (@ref focus_job_id,
 * @ref pending_slug) are plain strings one screen sets and another reads.
 *
 * Lifetime: @ref caller and @ref router are references owned by @ref App and
 * outlive every screen; a screen must not retain them past its own destruction.
 */
struct ScreenContext
{
    ///< Runs commands off the UI thread and posts responses back (subtask 03).
    ///< Owned by @ref App; must outlive every screen.
    AsyncCaller& caller;

    ///< The current session identity/mode for status and read-only rendering.
    SessionInfo session;

    ///< Shared navigation state machine (D11). Owned by @ref App; must outlive
    ///< every screen.
    Router& router;

    ///< Process-level presentation/safety options (theme, read-only, --yes).
    TuiOptions options;

    ///< `true` while a text `Input` has focus, so the global hotkey handler
    ///< ignores digits/`:`/`q` (typing `1` in a slug field must not switch
    ///< tab). A form/filter component sets and clears this.
    bool text_input_active = false;

    ///< Job id handed from Import/Export to the Jobs screen so it can focus a
    ///< freshly-started job; empty when none is pending.
    std::string focus_job_id;

    ///< Slug handed from Links to Link detail; empty when none is pending.
    std::string pending_slug;
};

/**
 * @brief Builds the @ref CapabilityModel a screen uses as its single
 * enable/disable decision (subtask 18 success criterion 4).
 *
 * The returned model mirrors the session's read-only state and resolves each
 * request's *effective* permission through @ref
 * AsyncCaller::required_permission (so an argument-dependent raise like
 * `link.list status=all` is honored, D16). It captures @p context's @ref
 * ScreenContext::caller by reference; that caller is owned by @ref App and
 * outlives every screen, so a model kept by a screen (or its view context) is
 * safe for the screen's whole lifetime. Screens call `capability.can_run(...)`
 * instead of reading the session mode directly, so the capability decision
 * lives in exactly one place.
 *
 * @param context The screen context whose session and caller the model wraps.
 * @return A @ref CapabilityModel bound to @p context's session and caller.
 */
inline CapabilityModel capability_for(const ScreenContext& context)
{
    AsyncCaller& caller = context.caller;
    // The read-only mode is the process flag (`TuiOptions::read_only`), which
    // `tui_entry` derives identically to the session's `ControlSet::User`; use
    // it so the capability agrees with the session identity carried for the
    // frame role/badge.
    SessionInfo session = context.session;
    session.read_only = context.options.read_only;
    return CapabilityModel(session,
                           [&caller](const commands::CommandRequest& request)
                           { return caller.required_permission(request); });
}

/**
 * @brief The interface every screen implements so it plugs into the frame
 * without the frame knowing the concrete screen (D1).
 *
 * @ref App registers screens through this interface; the frame draws
 * @ref component in the body and @ref hints in the footer, and calls
 * @ref on_enter / @ref on_leave as the active screen changes. A screen builds
 * `CommandRequest`s and renders `CommandResponse`s via its @ref ScreenContext;
 * it owns no business logic (ADR 0003 Decision 2).
 */
class IScreen
{
public:
    /**
     * @brief Virtual destructor; screens are held through this base.
     */
    virtual ~IScreen() = default;

    /**
     * @brief Returns the screen this object renders.
     *
     * @return The @ref ScreenId identifying this screen; stable for the
     * object's lifetime and used by @ref App to map router state to a screen.
     */
    virtual ScreenId id() const = 0;

    /**
     * @brief Returns the FTXUI component drawn in the frame body.
     *
     * @return The screen's root component; never null. @ref App places it in
     * the body region of the D1 frame.
     */
    virtual ftxui::Component component() = 0;

    /**
     * @brief Returns the screen-specific footer key hints.
     *
     * The frame shows these before the global hints (@ref global_hints). A
     * screen with no hints of its own returns an empty vector.
     *
     * @return The footer hints for this screen, in display order.
     */
    virtual std::vector<KeyHint> hints() const = 0;

    /**
     * @brief Called when this screen becomes active.
     *
     * The default is a no-op; a data-backed screen overrides it to refresh via
     * @ref AsyncCaller.
     */
    virtual void on_enter() {}

    /**
     * @brief Called when this screen stops being active.
     *
     * The default is a no-op; a screen overrides it to cancel polling or
     * release focus.
     */
    virtual void on_leave() {}
};

}  // namespace url_shortener::tui

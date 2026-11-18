#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/clock.hpp>

namespace url_shortener::tui
{

/**
 * @file toast.hpp
 * @brief The FTXUI-free model of the D10 error/info toast: the @ref Toast
 * value, the bounded @ref ToastQueue with deterministic expiry, and
 * @ref toast_from_response which turns a command envelope into a toast.
 *
 * This header belongs to `url_shortener_tui_model` (ADR 0003 rule 6): it holds
 * no FTXUI and no colour. `toast_view.cpp` renders a @ref Toast into FTXUI
 * elements and is the T-110 sink that runs every server/stored string
 * (@ref Toast::title, @ref Toast::body, @ref Toast::raw_envelope) through
 * `text_fit::sanitize_for_terminal` before `text()`. Strings are stored here
 * intact (never pre-sanitized) so the raw envelope shown by `[d]etails` is
 * exactly what the client returned (success criterion 3).
 *
 * @note The subtask spec sketched @ref Toast::expires as a
 * `steady_clock::time_point` and the queue over a `core::Clock`; the project's
 * verified clock seam is the global `IClock` (`core/clock.hpp`) whose `now()`
 * yields a `system_clock::time_point` and whose `ManualClock` drives
 * deterministic tests, so this header uses that type throughout. The behaviour
 * (info/success expire after a TTL, errors persist) is unchanged.
 */

/**
 * @brief The severity/intent of a @ref Toast, which selects its style and
 * whether it auto-expires.
 *
 * Carries meaning without colour (the mono theme shows no colour): the title
 * glyph/word (`"! <code>"` for @ref error, a trailing `"ok"` for @ref success)
 * is what the operator reads, so the kind is an accessibility-safe signal.
 */
enum class ToastKind
{
    info,     ///< Neutral notice; auto-expires after the queue TTL.
    success,  ///< A command succeeded; auto-expires after the queue TTL.
    error,    ///< A command failed; persists until explicitly dismissed.
};

/**
 * @brief One toast: a rendered-once feedback message derived from a command
 * response (or a locally-built notice).
 *
 * A pure value type that owns all of its strings; copying is cheap. Every
 * string field except @ref expires is server/stored data and is sanitized at
 * the render sink (T-110), never here.
 */
struct Toast
{
    ///< Severity/intent; selects the style role and expiry behaviour.
    ToastKind kind = ToastKind::info;

    ///< One-line heading. For an error this is `"! <error.code>"` (e.g.
    ///< `"! slug_conflict"`); for a success it is `"<command> ok"`.
    std::string title;

    ///< The human body. For an error this is `error.message`, truncated to a
    ///< display-safe length (see @ref toast_from_response); empty for a plain
    ///< success. Sanitized at the render sink before display.
    std::string body;

    ///< The command that produced this toast (e.g. `"link.create"`), shown in
    ///< the toast footer; empty when not applicable.
    std::string source_command;

    ///< The response correlation id, shown in the footer for support
    ///< ("id c-42"); empty when not applicable.
    std::string request_id;

    ///< The full response envelope text, kept intact for the `[d]etails`
    ///< view. Never truncated; sanitized only when shown (T-110).
    std::string raw_envelope;

    ///< When an auto-expiring toast is due to disappear. Assigned by
    ///< @ref ToastQueue::push from the injected clock; unused (and left at its
    ///< default epoch) for @ref ToastKind::error, which never auto-expires.
    std::chrono::system_clock::time_point expires {};
};

/**
 * @brief Builds a @ref Toast from a command's response envelope.
 *
 * On success (`r.ok`) produces a @ref ToastKind::success toast titled
 * `"<command> ok"` with an empty body. On failure produces a
 * @ref ToastKind::error toast titled `"! <error.code>"` with the envelope's
 * `error.message` as the body; a failure whose `error.code` is `none`
 * (an ambiguous/self-contradictory envelope) is treated as a generic
 * `"! internal"` toast so a malformed response never renders as a success and
 * never crashes (threat model T-113). The body is truncated to at most 200
 * code points with a trailing `"..."` for display; @ref Toast::raw_envelope
 * keeps the complete envelope intact. @ref Toast::expires is left at its
 * default here and assigned by @ref ToastQueue::push.
 *
 * @param command The command name that produced @p r, copied into
 * @ref Toast::source_command and used in a success title.
 * @param request_id The response correlation id, copied into
 * @ref Toast::request_id.
 * @param r The response envelope to classify; its strings are copied verbatim
 * (not sanitized here, T-110).
 * @return The derived toast, ready to hand to @ref ToastQueue::push.
 */
Toast toast_from_response(const std::string& command,
                          const std::string& request_id,
                          const commands::CommandResponse& r);

/**
 * @brief A bounded, clock-driven queue of visible toasts (model layer).
 *
 * Holds at most three visible toasts; pushing a fourth evicts the oldest.
 * Auto-expiring toasts (info/success) disappear from @ref visible once the
 * injected clock passes their deadline; error toasts persist until
 * @ref dismiss_front removes them. The queue is FTXUI-free and single-threaded
 * (it is mutated only on the UI thread); `toast_view.cpp` renders
 * @ref visible().
 */
class ToastQueue
{
public:
    ///< The most toasts shown at once; a push beyond this evicts the oldest.
    static constexpr std::size_t k_max_visible = 3;

    /**
     * @brief Constructs the queue over a clock and an auto-expiry TTL.
     *
     * @param clock Time source used to stamp and expire auto-expiring toasts;
     * a reference captured for the queue's lifetime, so @p clock must outlive
     * this queue (inject a `ManualClock` in tests for deterministic expiry).
     * @param ttl How long an info/success toast stays visible after it is
     * pushed; defaults to six seconds. A non-positive TTL makes such toasts
     * expire on the next @ref tick.
     */
    explicit ToastQueue(const IClock& clock,
                        std::chrono::seconds ttl = std::chrono::seconds(6));

    /**
     * @brief Appends a toast, stamping its expiry and enforcing the cap.
     *
     * For @ref ToastKind::info and @ref ToastKind::success, @ref Toast::expires
     * is set to `clock.now() + ttl`; for @ref ToastKind::error it is left
     * untouched (errors never auto-expire). If the queue already holds
     * @ref k_max_visible toasts the oldest (front) is dropped so the newest is
     * always visible.
     *
     * @param t The toast to add; moved into the queue.
     */
    void push(Toast t);

    /**
     * @brief Expires any auto-expiring toast whose deadline has passed.
     *
     * Removes every @ref ToastKind::info / @ref ToastKind::success toast whose
     * @ref Toast::expires is `<= clock.now()`; @ref ToastKind::error toasts are
     * never removed here. Safe to call every frame.
     */
    void tick();

    /**
     * @brief The currently visible toasts, oldest first.
     *
     * @return A reference to the internal deque; valid until the next mutating
     * call (@ref push, @ref tick, @ref dismiss_front).
     */
    const std::deque<Toast>& visible() const;

    /**
     * @brief Removes the oldest (front) toast, if any.
     *
     * The operator's `Esc` maps here, so an error toast can be cleared. A no-op
     * when the queue is empty.
     */
    void dismiss_front();

private:
    ///< Time source for stamping and expiring auto-expiring toasts; not owned.
    const IClock& m_clock_;

    ///< How long an info/success toast stays visible after being pushed.
    std::chrono::seconds m_ttl_;

    ///< The visible toasts, oldest at the front; bounded by @ref k_max_visible.
    std::deque<Toast> m_toasts_;
};

}  // namespace url_shortener::tui

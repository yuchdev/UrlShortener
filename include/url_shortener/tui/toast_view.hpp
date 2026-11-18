#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/toast.hpp>

namespace url_shortener::tui
{

/**
 * @brief Renders one toast as a bordered box (D10 right-hand panel).
 *
 * Builds a `window(title, body)` whose title is `"! <code>"`/`"<command> ok"`
 * and whose body carries the message plus a `(<command>, id <id>) [d]etails`
 * footer. When @p show_details is `true` the box also shows the pretty-printed
 * raw envelope. T-110: @ref Toast::title, @ref Toast::body and
 * @ref Toast::raw_envelope are each passed through `sanitize_for_terminal`
 * before `text()`, so no stored control/bidi/zero-width byte reaches the grid;
 * the box is styled by role (`error`/`ok`/`banner`) so the mono theme still
 * conveys severity through the title text, not colour alone.
 *
 * @param t The toast to render.
 * @param show_details `true` to append the sanitized raw envelope.
 * @param theme The active theme, used for the box style.
 * @return The toast box element; never null.
 */
ftxui::Element
render_toast(const Toast& t, bool show_details, const Theme& theme);

/**
 * @brief Renders the whole visible toast stack, anchored bottom-right.
 *
 * Stacks @ref ToastQueue::visible oldest-to-newest in the lower-right of the
 * frame body (never over the footer, ADR 0003 D10/step 2); @p show_details
 * applies to the newest (front-most) error toast. Returns an empty element when
 * there are no visible toasts.
 *
 * @param queue The queue whose visible toasts to render.
 * @param show_details `true` to expand the newest error toast's raw envelope.
 * @param theme The active theme.
 * @return The bottom-right-anchored stack element; never null.
 */
ftxui::Element
render_toasts(const ToastQueue& queue, bool show_details, const Theme& theme);

/**
 * @brief A toast-overlay component layered over the frame body.
 *
 * Renders @ref render_toasts for @p queue and reacts to keys only while at
 * least one toast is visible: `d` toggles the raw-envelope `[d]etails` view and
 * `Esc` dismisses the newest toast (@ref ToastQueue::dismiss_front). It never
 * swallows other keys, so navigation still flows to the screen underneath.
 *
 * @param queue The toast queue to render and mutate; a reference captured for
 * the component's lifetime, so it must outlive the returned component.
 * @param theme The active theme; a reference captured for the component's
 * lifetime, so it must outlive the returned component.
 * @return The overlay component; never null.
 */
ftxui::Component toast_overlay(ToastQueue& queue, const Theme& theme);

}  // namespace url_shortener::tui

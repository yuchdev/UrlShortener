#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include <ftxui/component/component_base.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/form_model.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/toast.hpp>

namespace url_shortener::tui
{

/**
 * @file form_view.hpp
 * @brief The FTXUI half of the generic form builder (D5, subtask 06.0/08):
 * @ref make_field_widget renders one @ref FormField per @ref ArgKind and
 * @ref make_form_component frames the fields, buttons, live validation line and
 * `Equivalent:` footer and wires the confirm/submit flow.
 *
 * Belongs to `url_shortener_tui` (it links FTXUI). The view never builds JSON
 * (only @ref FormModel::to_request does, contracts C1/C8) and is a **T-110
 * render sink**: every display string it emits - field editing buffers (raw
 * server values after @ref FormModel::prefill_from_result), @ref ArgInfo::doc
 * and @ref ArgInfo::enum_values, @ref FormField::error, @ref
 * FormModel::validation_summary, server error text and the `Equivalent:` footer
 * - is passed through `text_fit::sanitize_for_terminal` before `text()`. In
 * particular the fields render a **sanitized echo** of their buffers rather
 * than a raw `ftxui::Input` (which renders content raw), so no stored control /
 * bidi / zero-width byte can reach the grid.
 */

/**
 * @brief Everything @ref make_form_component needs from the owning screen.
 *
 * A non-owning aggregate the screen fills; every reference member is owned by
 * the screen (or by @ref App above it) and must outlive the returned component,
 * as must whatever the @ref on_success / @ref on_cancel / @ref
 * on_text_input_changed callables capture.
 */
struct FormViewContext
{
    ///< Runs the submit off the UI thread and posts the response back. Must
    ///< outlive the component.
    AsyncCaller& caller;

    ///< Decides the confirmation for a mutating submit. Must outlive the
    ///< component.
    ConfirmPolicy& policy;

    ///< Receives the submit's success / error / job toast. Must outlive the
    ///< component.
    ToastQueue& toasts;

    ///< The active theme used for labels, disabled controls and the dialog.
    ///< Non-owning; must outlive the component.
    const Theme& theme;

    ///< `true` to run as `ControlSet::User` (subtask 18): the submit button is
    ///< disabled and dim for a command whose permission is not `"read"`, and
    ///< the form sends nothing (ADR 0003 rule 5).
    bool read_only = false;

    ///< Title shown on the form window, e.g. `"New link"`; the command name is
    ///< appended in parentheses. Composition text; empty falls back to the
    ///< command name. Still sanitized at the sink.
    std::string title;

    ///< Called with the response after a successful submit so the screen can
    ///< refresh / navigate; empty when the screen needs no success hook.
    std::function<void(const commands::CommandResponse&)> on_success;

    ///< Called when the operator cancels (Cancel button, or a confirmed discard
    ///< prompt); empty when the screen needs no cancel hook.
    std::function<void()> on_cancel;

    ///< Called with `true` when a text field gains focus and `false` when it
    ///< loses it, so the screen can mirror it into
    ///< `ScreenContext::text_input_active` and keep digit/`:` hotkeys quiet
    ///< while the operator types (subtask 04). Empty when the screen does not
    ///< track it.
    std::function<void(bool focused)> on_text_input_changed;
};

/**
 * @brief Builds the interactive widget for one field, chosen by its
 * @ref ArgKind.
 *
 * The widget edits @p model's field at @p index through the model's mutators
 * (so PATCH `untouched`/`set`/`cleared` state stays authoritative) and renders
 * a
 * **sanitized echo** of the buffer (T-110). The mapping is: text / integer /
 * timestamp / list / map / member-less object -> a single-line echoed input
 * (integer rejects non-digit keystrokes); boolean -> a `[x]`/`[ ]` checkbox
 * toggled with Space; enumeration of <= 3 values -> a horizontal radio cycled
 * with Left/Right/Space; enumeration of > 3 values -> a dropdown-style value
 * cycled the same way; object with members -> a row of placeholder member cells
 * navigated with Left/Right. A nullable field adds a clear affordance (Delete)
 * that emits an explicit `null`.
 *
 * @param model The form whose field is edited; held by reference, must outlive
 * the widget.
 * @param index The field index into @ref FormModel::fields; an out-of-range
 * index yields a harmless inert widget.
 * @param theme The theme used for focus / disabled styling; held by reference,
 * must outlive the widget.
 * @return A never-null, focusable component rendering and editing the one
 * field.
 */
ftxui::Component make_field_widget(FormModel& model,
                                   std::size_t index,
                                   const Theme& theme);

/**
 * @brief Builds the complete D5 form body: fields, buttons, validation line and
 * `Equivalent:` footer, with the confirm/submit flow wired.
 *
 * The returned component lays out one @ref make_field_widget per field in a
 * scrolling (`yframe`) label/hint grid, a `[ <verb> ] [ Cancel ]` button row
 * (verb from the command: `Create` / `Save` / `Run`), the live
 * @ref FormModel::validation_summary line and the sanitized `Equivalent:` CLI
 * footer. Keys: Tab / arrows move focus, Space / Left / Right edit the focused
 * non-text widget, Ctrl-S submits from anywhere, Esc cancels (with a discard
 * prompt only when a field is touched), and `y` (when no text field is focused)
 * shows the full footer in a toast. A submit validates, builds the one request
 * with @ref FormModel::to_request and routes it through @ref run_mutation
 * (every mutating submit confirms; a read command submits directly). The submit
 * button is disabled while @p ctx reports read-only for a write command or
 * while a call is in flight (double Ctrl-S sends one request).
 *
 * @param model The form to render and submit; held by reference, must outlive
 * the component.
 * @param ctx The owning screen's collaborators and callbacks; held by
 * reference, must outlive the component (see @ref FormViewContext).
 * @return A never-null component suitable for a screen body.
 */
ftxui::Component make_form_component(FormModel& model, FormViewContext& ctx);

}  // namespace url_shortener::tui

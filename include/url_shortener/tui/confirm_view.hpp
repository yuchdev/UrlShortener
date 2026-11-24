#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/theme.hpp>

namespace url_shortener::tui
{

/**
 * @brief Renders the confirm dialog as a centered modal element (D10 left box).
 *
 * Builds `window(title, ...)` with the prompt, a single-line token field
 * showing the operator's current input for a @ref ConfirmKind::type_token
 * request, and the `[ <verb> ] [ Cancel ]` buttons. The action button is drawn
 * dim (disabled) until @ref ConfirmModel::can_submit is `true`, so the
 * disabled/enabled state is visible without colour (mono-safe). T-110:
 * @ref ConfirmRequest::title and @ref ConfirmRequest::prompt (which already
 * embed the sanitized slug) and the displayed input are passed through
 * `sanitize_for_terminal` before `text()`; the raw token is never shown.
 *
 * @param model The dialog state to render.
 * @param theme The active theme, used for the disabled-button style.
 * @return The centered, cleared-under modal element; never null.
 */
ftxui::Element render_confirm(const ConfirmModel& model, const Theme& theme);

/**
 * @brief A confirm-dialog component driving @p model (D10 left box).
 *
 * Renders @ref render_confirm and handles input directly (mirroring the help
 * overlay's self-contained component so the model/render/golden path stays
 * terminal-free and deterministic): for a @ref ConfirmKind::type_token request
 * it appends typed characters to a single-line token input and deletes on
 * Backspace (keeping the raw bytes for the comparison); `Enter` submits only
 * when @ref ConfirmModel::can_submit is `true` (ignored otherwise) and `Esc`
 * cancels. Once the model has a @ref ConfirmModel::result it renders nothing and
 * ignores input.
 *
 * @param model The dialog state to drive; a reference captured for the
 * component's lifetime, so it must outlive the returned component.
 * @param theme The active theme; a reference captured for the component's
 * lifetime, so it must outlive the returned component.
 * @return The dialog component; never null.
 */
ftxui::Component confirm_dialog(ConfirmModel& model, const Theme& theme);

}  // namespace url_shortener::tui

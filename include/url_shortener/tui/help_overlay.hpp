#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/help_model.hpp>
#include <url_shortener/tui/theme.hpp>

namespace url_shortener::tui
{

/**
 * @brief Renders the F1 help overlay body as a centered modal element.
 *
 * Builds a `window("Help", ...)` listing one row per @ref HelpModel::entries
 * row (`keys` then `label`), centered and drawn over a cleared background so it
 * floats above the frame. T-110: every row's `keys` and `label` is passed
 * through `sanitize_for_terminal` and width-fitted before it reaches FTXUI
 * `text()`, so a hint a screen contributes from server-derived text can never
 * inject a terminal-control sequence. The key column is styled with
 * @p theme `hint`; in the @ref ThemeName::mono theme the frame root's
 * `ascii_filter` folds this modal's border to ASCII along with the rest of the
 * frame.
 *
 * @param model The help content to render.
 * @param theme The active theme, used for the key-column style.
 * @return The centered, cleared-under overlay element; never null.
 */
ftxui::Element render_help_overlay(const HelpModel& model, const Theme& theme);

/**
 * @brief An F1 help-overlay component gated by a visibility flag.
 *
 * While `*visible` is `true` the component renders @ref render_help_overlay and
 * swallows every event, closing (setting `*visible` to `false`) on `Esc`, `F1`
 * or `?`; while `*visible` is `false` it renders nothing and ignores events, so
 * it can be stacked over the frame unconditionally (ADR 0003 D1 footer form).
 * Swallowing input while open means a navigation key such as `j` does not leak
 * to the screen underneath.
 *
 * @param model The help content to show; moved in and snapshotted for the
 * component's lifetime.
 * @param visible Non-owning pointer to the caller's visibility flag toggled by
 * the frame's F1 handler; must outlive the returned component and must not be
 * null.
 * @return The overlay component; never null.
 */
ftxui::Component help_overlay(HelpModel model, bool* visible);

}  // namespace url_shortener::tui

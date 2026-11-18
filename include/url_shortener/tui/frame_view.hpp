#pragma once

#include <vector>

#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/frame_model.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/screen_id.hpp>
#include <url_shortener/tui/theme.hpp>

namespace url_shortener::tui
{

/// The smallest terminal the frame renders into; below this the frame is
/// replaced by a "too small" message (D1 / step 7).
inline constexpr int k_min_frame_width = 80;

/// @see k_min_frame_width.
inline constexpr int k_min_frame_height = 24;

/**
 * @brief Renders the D1 persistent frame around a screen body.
 *
 * Layout (step 3): a bordered `window` whose title row is
 * `title | filler | [F1 Help]`, containing a vbox of the tab bar, an optional
 * one-line inverted memory-store banner, the body (flexed to fill), and the
 * key-hint footer. The footer shows @p footer_hints followed by the global
 * hints (@ref global_hints). The active tab is drawn inverted.
 *
 * When @p width or @p height is below the 80x24 minimum, the frame is replaced
 * by a single centered "terminal too small (need 80x24)" element so a tiny
 * terminal never renders a broken layout.
 *
 * @param model The frame text model (title, banner, tabs).
 * @param active_tab The tab to highlight; pass @ref Router::active_tab.
 * @param body The active screen's body element; placed in the frame body.
 * @param footer_hints The active screen's own hints, shown before the globals.
 * @param width Current terminal width in cells.
 * @param height Current terminal height in cells.
 * @param theme The active theme (subtask 05): its roles style the title, tabs
 * and banner, and when @ref Theme::ascii_only is set (the `mono` theme) the
 * whole frame is wrapped in @ref ascii_filter so its borders render as ASCII.
 * Defaults to the dark theme, so pre-05 callers are unaffected.
 * @return The frame element, or the too-small message when under 80x24.
 */
ftxui::Element render_frame(const FrameModel& model,
                            ScreenId active_tab,
                            ftxui::Element body,
                            const std::vector<KeyHint>& footer_hints,
                            int width,
                            int height,
                            const Theme& theme = theme_for(ThemeName::dark));

}  // namespace url_shortener::tui

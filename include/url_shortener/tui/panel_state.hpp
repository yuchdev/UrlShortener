#pragma once

namespace url_shortener::tui
{

/**
 * @brief Load state of one independent data panel in the TUI (ADR 0003 D2/D13).
 *
 * A panel is @ref loading until the command(s) that feed it answer, then
 * @ref ready on success or @ref unavailable on any failure. The Dashboard
 * (subtask 06.0/17) owns six such panels, each advanced independently so one
 * failing command degrades only its own panel (success criterion 2); the
 * Settings screen (subtask 06.0/21) has a single panel, so "degrade only the
 * failing panel" collapses to "degrade the one panel there is".
 *
 * This is the canonical, shared definition (model library, FTXUI-free): both
 * @ref url_shortener::tui::DashboardModel and @ref
 * url_shortener::tui::SettingsModel include it so there is exactly one @ref
 * PanelState type across the TUI (no ODR clash). An @ref unavailable panel is
 * never hidden; it renders an `"n/a"` placeholder plus the failure reason so
 * the operator always sees that the figure is missing rather than zero.
 */
enum class PanelState
{
    ///< The feeding command(s) are in flight and no data is shown yet.
    loading,

    ///< Every feeding command succeeded; the panel's data is valid and
    ///< rendered.
    ready,

    ///< A feeding command failed; the panel shows `"n/a"` and the error reason.
    unavailable
};

}  // namespace url_shortener::tui

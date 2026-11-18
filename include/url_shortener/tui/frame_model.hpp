#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen_id.hpp>

namespace url_shortener::tui
{

/**
 * @brief The pure text model of the D1 persistent frame (title, banner, tabs).
 *
 * Derives the title bar, the memory-store banner and the tab-bar labels from
 * the session identity (@ref SessionInfo) and two values read once from
 * `system.info` (the backend label and its `persistent` flag). It holds no
 * FTXUI; `frame_view.cpp` turns this model into elements. Keeping it a plain
 * value type lets the frame text be asserted with no terminal (ADR 0003 rule 6)
 * and keeps config values off the client (only the server-provided *label* is
 * shown, never a DSN or path, ADR 0003 D1 / step 2).
 *
 * T-110: @ref title embeds @p backend_label, which is server-provided data.
 * This model only length-bounds it (@ref title truncates to 60 chars); the
 * render sink must additionally route it through `sanitize_for_terminal`
 * (subtask 09) once that helper lands. See `frame_view.cpp`.
 */
class FrameModel
{
public:
    /**
     * @brief Builds the frame model from the session and backend facts.
     *
     * @param session The current session; its @ref SessionInfo::read_only flag
     * selects the `local/Admin` vs `local/User` role shown in the title.
     * @param backend_label The metadata backend's human label from
     * `system.info` (e.g. `"sqlite (links.db)"`); server data, shown truncated
     * and otherwise rendered through the T-110 sanitizer at the sink.
     * @param persistent `true` when the backend survives process exit; when
     * `false`, @ref banner yields the memory-store warning.
     * @param theme_name The active theme name (carried for subtask 05; it does
     * not affect the text this model produces).
     */
    FrameModel(SessionInfo session,
               std::string backend_label,
               bool persistent,
               std::string theme_name);

    /**
     * @brief Returns the title-bar text.
     *
     * Format: `"url_shortener -- backend: <label> -- user: local/<role>"`,
     * where `<role>` is `Admin` for a read/write session and `User` for a
     * read-only one, and `<label>` is the backend label truncated to 60
     * display characters with a trailing `"..."` when it was longer.
     *
     * @return The composed title; always non-empty.
     */
    std::string title() const;

    /**
     * @brief Returns the memory-store banner when the backend is not persistent.
     *
     * @return `"memory store - data is lost on exit"` when the backend is
     * non-persistent; `std::nullopt` otherwise (no banner row is drawn).
     */
    std::optional<std::string> banner() const;

    /**
     * @brief Returns the D1 tab-bar labels with their active flags.
     *
     * The eight tabs in D1 order (`[1]Dashboard` .. `[7]Settings`, `[:]Palette`)
     * each paired with whether it is the active tab. Pass the router's
     * @ref Router::active_tab so a nested screen still highlights its owning tab.
     *
     * @param active The tab (or owning tab) currently shown.
     * @return Eight `(label, is_active)` pairs in display order; exactly one
     * pair is active when @p active is one of the eight tab screens.
     */
    std::vector<std::pair<std::string, bool>> tabs(ScreenId active) const;

private:
    ///< The session whose role drives the title's `local/<role>` suffix.
    SessionInfo m_session_;

    ///< Server-provided backend label; shown truncated and sanitized at render.
    std::string m_backend_label_;

    ///< `true` when the backend is persistent; drives @ref banner.
    bool m_persistent_;

    ///< Active theme name; stored for subtask 05, unused by the text model.
    std::string m_theme_name_;
};

}  // namespace url_shortener::tui

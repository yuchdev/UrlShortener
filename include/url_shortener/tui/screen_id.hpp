#pragma once

#include <array>
#include <string>

namespace url_shortener::tui
{

/**
 * @brief Every screen the TUI can show, i.e. every node of the D11 state
 * machine (ADR 0003) plus the Analytics and Settings tab slots of D1.
 *
 * The enumerators are the single source of truth the @ref Router edge table and
 * the @ref FrameModel tab bar are built from; @ref all_screen_ids lists them in
 * declaration order for exhaustive iteration. Enumerator order is not
 * contractual except that @ref all_screen_ids mirrors it; never persist a raw
 * value. New screens append here and are wired into @ref Router::is_legal.
 */
enum class ScreenId
{
    ///< D2 Dashboard; the start screen and tab `[1]`.
    dashboard,

    ///< D3 Links table; tab `[2]`.
    links,

    ///< D4 Link detail / stats / preview (reached from Links).
    link_detail,

    ///< D5 New / edit link form (reached from Links, Link detail, Tables).
    link_form,

    ///< D6 Tables browser; tab `[3]`.
    tables,

    ///< Row-JSON viewer over a Tables row (reached from Tables).
    row_json,

    ///< D7 Import / export; tab `[4]`.
    import_export,

    ///< D8 Jobs; tab `[5]`.
    jobs,

    ///< Job detail / full error envelope (reached from Jobs).
    job_detail,

    ///< D1 `[6]` Analytics placeholder tab (ADR 0003 non-goal in 0006).
    analytics,

    ///< D13 Settings; tab `[7]` (a real screen registered by subtask 21).
    settings,

    ///< D9 Command Palette; tab `[:]`.
    palette,

    ///< Generated form for an arbitrary command picked from the Palette.
    generic_form,

    ///< D10 Confirm modal (reached from a mutating form); see @ref Router.
    confirm
};

/**
 * @brief The number of distinct @ref ScreenId values.
 *
 * Kept in sync with the enumerator list by @ref all_screen_ids; used by the
 * exhaustive 14x14 router edge test.
 */
inline constexpr std::size_t k_screen_id_count = 14;

/**
 * @brief Returns every @ref ScreenId value in declaration order.
 *
 * @return A fixed array of all @ref k_screen_id_count screens, letting callers
 * (and the router test) iterate the full set without hard-coding each name.
 */
inline const std::array<ScreenId, k_screen_id_count>& all_screen_ids()
{
    static const std::array<ScreenId, k_screen_id_count> ids = {
        ScreenId::dashboard,     ScreenId::links,
        ScreenId::link_detail,   ScreenId::link_form,
        ScreenId::tables,        ScreenId::row_json,
        ScreenId::import_export, ScreenId::jobs,
        ScreenId::job_detail,    ScreenId::analytics,
        ScreenId::settings,      ScreenId::palette,
        ScreenId::generic_form,  ScreenId::confirm};
    return ids;
}

/**
 * @brief Returns the stable `lower_case` name of a screen.
 *
 * Used for diagnostics and test messages only, never parsed back. The mapping
 * is total: an out-of-range value (never produced by well-formed code) returns
 * `"unknown"` rather than reading out of bounds.
 *
 * @param id The screen to name.
 * @return The screen's identifier as a string, e.g. `"link_detail"`;
 * `"unknown"` for an unrecognized value.
 */
inline std::string to_string(ScreenId id)
{
    switch (id)
    {
    case ScreenId::dashboard: return "dashboard";
    case ScreenId::links: return "links";
    case ScreenId::link_detail: return "link_detail";
    case ScreenId::link_form: return "link_form";
    case ScreenId::tables: return "tables";
    case ScreenId::row_json: return "row_json";
    case ScreenId::import_export: return "import_export";
    case ScreenId::jobs: return "jobs";
    case ScreenId::job_detail: return "job_detail";
    case ScreenId::analytics: return "analytics";
    case ScreenId::settings: return "settings";
    case ScreenId::palette: return "palette";
    case ScreenId::generic_form: return "generic_form";
    case ScreenId::confirm: return "confirm";
    }
    return "unknown";
}

}  // namespace url_shortener::tui

#include <utility>

#include "url_shortener/tui/frame_model.hpp"

#include <url_shortener/tui/capability.hpp>
#include <url_shortener/tui/text_fit.hpp>

namespace url_shortener::tui
{

namespace
{

/// Maximum display width of the backend label in the title bar (D1 step 2).
constexpr std::size_t k_backend_label_max = 60;

/// The fixed title prefix that precedes the backend label.
constexpr char k_title_prefix[] = "url_shortener -- backend: ";

/// The fixed title segment that precedes the role (`Admin`/`User`).
constexpr char k_role_prefix[] = " -- user: local/";

/// Sanitizes the server-provided backend label and fits it to @p budget
/// columns.
///
/// T-110: @p value comes from `system.info` (untrusted server data), so it is
/// first run through @ref sanitize_for_terminal (dropping ESC/CSI/OSC/DCS, C0/
/// C1, bidi and zero-width code points) and only then width-fitted with
/// @ref truncate_to_width (UTF-8-aware, ellipsis on cut). Doing both in the
/// model means every consumer of @ref FrameModel::title gets a safe, bounded
/// label with no per-sink duplication.
std::string safe_label(const std::string& value, std::size_t budget)
{
    return truncate_to_width(sanitize_for_terminal(value), budget);
}

/// Composes the full title with the backend label fitted to @p label_budget.
///
/// The role (`Admin`/`User`) and, for a read-only session, the `[READ-ONLY]`
/// badge are always appended in full; only the backend label is width-bounded,
/// so the operator's role and badge are never the part that gets clipped
/// (subtask 18 review E). The badge text comes from @ref CapabilityModel so the
/// title and the status gating share one source of truth.
std::string compose_title(const SessionInfo& session,
                          const std::string& backend_label,
                          std::size_t label_budget)
{
    const char* role = session.read_only ? "User" : "Admin";
    std::string title = std::string(k_title_prefix)
        + safe_label(backend_label, label_budget) + k_role_prefix + role;
    const std::string badge = CapabilityModel(session).badge();
    if (!badge.empty()) {
        title += " [" + badge + "]";
    }
    return title;
}

/// The display width of the fixed (non-label) title parts for @p session: the
/// prefix, the role segment and, when read-only, the ` [READ-ONLY]` badge. All
/// are ASCII, so byte length equals display width.
std::size_t title_overhead(const SessionInfo& session)
{
    const char* role = session.read_only ? "User" : "Admin";
    std::size_t overhead = std::string(k_title_prefix).size()
        + std::string(k_role_prefix).size() + std::string(role).size();
    const std::string badge = CapabilityModel(session).badge();
    if (!badge.empty()) {
        overhead += badge.size() + 3;  // " [" + "]"
    }
    return overhead;
}

/// One D1 tab: its label and the screen whose active tab lights it up.
struct TabSpec
{
    const char* label;
    ScreenId id;
};

/// The eight D1 tabs in display order.
const TabSpec k_tabs[] = {
    {"[1]Dashboard", ScreenId::dashboard},
    {"[2]Links", ScreenId::links},
    {"[3]Tables", ScreenId::tables},
    {"[4]Import/Export", ScreenId::import_export},
    {"[5]Jobs", ScreenId::jobs},
    {"[6]Analytics", ScreenId::analytics},
    {"[7]Settings", ScreenId::settings},
    {"[:]Palette", ScreenId::palette},
};

}  // namespace

FrameModel::FrameModel(SessionInfo session,
                       std::string backend_label,
                       bool persistent,
                       std::string theme_name)
    : m_session_(std::move(session))
    , m_backend_label_(std::move(backend_label))
    , m_persistent_(persistent)
    , m_theme_name_(std::move(theme_name))
{
}

std::string FrameModel::title() const
{
    // The unbounded title: the backend label is capped at k_backend_label_max
    // only; the render sink (@ref title_within) applies the width budget.
    return compose_title(m_session_, m_backend_label_, k_backend_label_max);
}

std::string FrameModel::title_within(std::size_t max_cols) const
{
    // Reserve the fixed prefix + role + badge first, then fit the backend label
    // into whatever columns remain (never past the k_backend_label_max cap), so
    // the role and the `[READ-ONLY]` badge are always fully visible and the
    // label is the only part that shrinks (subtask 18 review E).
    const std::size_t overhead = title_overhead(m_session_);
    std::size_t budget = max_cols > overhead ? max_cols - overhead : 1;
    if (budget > k_backend_label_max) {
        budget = k_backend_label_max;
    }
    if (budget == 0) {
        budget = 1;
    }
    return compose_title(m_session_, m_backend_label_, budget);
}

std::optional<std::string> FrameModel::banner() const
{
    if (m_persistent_) {
        return std::nullopt;
    }
    return std::string("memory store - data is lost on exit");
}

std::vector<std::pair<std::string, bool>> FrameModel::tabs(
    ScreenId active) const
{
    std::vector<std::pair<std::string, bool>> out;
    out.reserve(sizeof(k_tabs) / sizeof(k_tabs[0]));
    for (const TabSpec& tab : k_tabs) {
        out.emplace_back(tab.label, tab.id == active);
    }
    return out;
}

}  // namespace url_shortener::tui

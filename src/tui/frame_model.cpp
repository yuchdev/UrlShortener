#include "url_shortener/tui/frame_model.hpp"

#include <utility>

namespace url_shortener::tui
{

namespace
{

/// Maximum display length of the backend label in the title bar (D1 step 2).
constexpr std::string::size_type k_backend_label_max = 60;

/// Truncates @p value to @ref k_backend_label_max chars, ending in "..." when
/// it was longer. Width is counted in bytes here; the render sink performs the
/// UTF-8-aware fit and T-110 sanitization (subtask 09).
std::string truncate_label(const std::string& value)
{
    if (value.size() <= k_backend_label_max)
    {
        return value;
    }
    return value.substr(0, k_backend_label_max - 3) + "...";
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
    : m_session_(std::move(session)),
      m_backend_label_(std::move(backend_label)),
      m_persistent_(persistent),
      m_theme_name_(std::move(theme_name))
{
}

std::string FrameModel::title() const
{
    const char* role = m_session_.read_only ? "User" : "Admin";
    return "url_shortener -- backend: " + truncate_label(m_backend_label_)
        + " -- user: local/" + role;
}

std::optional<std::string> FrameModel::banner() const
{
    if (m_persistent_)
    {
        return std::nullopt;
    }
    return std::string("memory store - data is lost on exit");
}

std::vector<std::pair<std::string, bool>>
FrameModel::tabs(ScreenId active) const
{
    std::vector<std::pair<std::string, bool>> out;
    out.reserve(sizeof(k_tabs) / sizeof(k_tabs[0]));
    for (const TabSpec& tab : k_tabs)
    {
        out.emplace_back(tab.label, tab.id == active);
    }
    return out;
}

}  // namespace url_shortener::tui

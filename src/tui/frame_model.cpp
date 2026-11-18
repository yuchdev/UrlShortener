#include "url_shortener/tui/frame_model.hpp"

#include <utility>

#include <url_shortener/tui/text_fit.hpp>

namespace url_shortener::tui
{

namespace
{

/// Maximum display width of the backend label in the title bar (D1 step 2).
constexpr std::size_t k_backend_label_max = 60;

/// Sanitizes the server-provided backend label and fits it into the title.
///
/// T-110: @p value comes from `system.info` (untrusted server data), so it is
/// first run through @ref sanitize_for_terminal (dropping ESC/CSI/OSC/DCS, C0/
/// C1, bidi and zero-width code points) and only then width-fitted with
/// @ref truncate_to_width (UTF-8-aware, ellipsis on cut). Doing both in the
/// model means every consumer of @ref FrameModel::title gets a safe, bounded
/// label with no per-sink duplication.
std::string safe_label(const std::string& value)
{
    return truncate_to_width(sanitize_for_terminal(value),
                             k_backend_label_max);
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
    return "url_shortener -- backend: " + safe_label(m_backend_label_)
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

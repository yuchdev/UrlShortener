#include "url_shortener/tui/help_overlay.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

namespace url_shortener::tui
{

namespace
{

/// Maximum display width of the key column; longer bindings are ellipsised.
constexpr std::size_t k_keys_width = 14;

/// Maximum display width of the label column.
constexpr std::size_t k_label_width = 44;

/// Maximum number of hint rows shown in the modal. Chosen to fit the 80x24
/// minimum terminal (two window borders plus centering margin); a longer list
/// is truncated with a trailing `... N more` line so the modal never overflows
/// the screen height.
constexpr std::size_t k_max_rows = 18;

/// Builds one `keys  label` row, sanitizing and width-fitting both columns.
ftxui::Element help_row(const HelpEntry& entry, const Theme& theme)
{
    using namespace ftxui;

    // T-110: a hint may be contributed by a screen from server-derived text, so
    // sanitize before width fitting (a truncated cell can never re-expose a
    // half-stripped escape).
    const std::string keys =
        truncate_to_width(sanitize_for_terminal(entry.keys), k_keys_width);
    const std::string label =
        truncate_to_width(sanitize_for_terminal(entry.label), k_label_width);

    return hbox({
        text(pad_to_width(keys, k_keys_width)) | to_decorator(theme.hint),
        text("  "),
        text(label),
    });
}

}  // namespace

ftxui::Element render_help_overlay(const HelpModel& model, const Theme& theme)
{
    using namespace ftxui;

    const std::vector<HelpEntry> entries = model.entries();

    Elements rows;
    if (entries.size() > k_max_rows)
    {
        // Show the first rows, then one summary line for the remainder, so the
        // modal height stays bounded regardless of how many hints are supplied.
        const std::size_t shown = k_max_rows - 1;
        rows.reserve(k_max_rows);
        for (std::size_t i = 0; i < shown; ++i)
        {
            rows.push_back(help_row(entries[i], theme));
        }
        rows.push_back(text("... " + std::to_string(entries.size() - shown)
                            + " more")
                       | to_decorator(theme.hint));
    }
    else
    {
        rows.reserve(entries.size());
        for (const HelpEntry& entry : entries)
        {
            rows.push_back(help_row(entry, theme));
        }
    }

    Element body = rows.empty() ? Element(text("(no actions)"))
                                : vbox(std::move(rows));

    Element win = window(text("Help"), std::move(body));
    return clear_under(center(std::move(win)));
}

ftxui::Component help_overlay(HelpModel model, Theme theme, bool* visible)
{
    using namespace ftxui;

    /// The overlay component: renders the modal and swallows input while open.
    class HelpOverlayComponent final : public ComponentBase
    {
    public:
        HelpOverlayComponent(HelpModel model, Theme theme, bool* visible)
            : m_model_(std::move(model)),
              m_theme_(std::move(theme)),
              m_visible_(visible)
        {
        }

        Element OnRender() override
        {
            if (m_visible_ == nullptr || !*m_visible_)
            {
                return text("");
            }
            Element overlay = render_help_overlay(m_model_, m_theme_);
            // In the mono theme fold the modal's borders to ASCII, matching the
            // frame root (the standalone component is not nested under it).
            if (m_theme_.ascii_only)
            {
                overlay = ascii_filter(std::move(overlay));
            }
            return overlay;
        }

        bool OnEvent(Event event) override
        {
            if (m_visible_ == nullptr || !*m_visible_)
            {
                return false;
            }
            if (event == Event::Escape || event == Event::F1 ||
                event == Event::Character("?"))
            {
                *m_visible_ = false;
            }
            return true;  // swallow every event while the overlay is open
        }

        bool Focusable() const override
        {
            return m_visible_ != nullptr && *m_visible_;
        }

    private:
        ///< Snapshot of the help content to render.
        HelpModel m_model_;

        ///< The theme used for styling and the mono ASCII fold.
        Theme m_theme_;

        ///< Non-owning visibility flag; owned by the frame, must outlive this.
        bool* m_visible_;
    };

    return Make<HelpOverlayComponent>(std::move(model), std::move(theme),
                                      visible);
}

ftxui::Component help_overlay(HelpModel model, bool* visible)
{
    return help_overlay(std::move(model), theme_for(ThemeName::dark), visible);
}

}  // namespace url_shortener::tui

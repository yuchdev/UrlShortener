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

}  // namespace

ftxui::Element render_help_overlay(const HelpModel& model, const Theme& theme)
{
    using namespace ftxui;

    const std::vector<HelpEntry> entries = model.entries();

    Elements rows;
    rows.reserve(entries.size());
    for (const HelpEntry& entry : entries)
    {
        // T-110: a hint may be contributed by a screen from server-derived text,
        // so sanitize before width fitting (a truncated cell can never re-expose
        // a half-stripped escape).
        const std::string keys =
            truncate_to_width(sanitize_for_terminal(entry.keys), k_keys_width);
        const std::string label = truncate_to_width(
            sanitize_for_terminal(entry.label), k_label_width);

        rows.push_back(hbox({
            text(pad_to_width(keys, k_keys_width)) | to_decorator(theme.hint),
            text("  "),
            text(label),
        }));
    }

    Element body = rows.empty() ? Element(text("(no actions)"))
                                : vbox(std::move(rows));

    Element win = window(text("Help"), std::move(body));
    return clear_under(center(std::move(win)));
}

ftxui::Component help_overlay(HelpModel model, bool* visible)
{
    using namespace ftxui;

    /// The overlay component: renders the modal and swallows input while open.
    class HelpOverlayComponent final : public ComponentBase
    {
    public:
        HelpOverlayComponent(HelpModel model, bool* visible)
            : m_model_(std::move(model)), m_visible_(visible)
        {
        }

        Element OnRender() override
        {
            if (m_visible_ == nullptr || !*m_visible_)
            {
                return text("");
            }
            return render_help_overlay(m_model_, theme_for(ThemeName::dark));
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

        ///< Non-owning visibility flag; owned by the frame, must outlive this.
        bool* m_visible_;
    };

    return Make<HelpOverlayComponent>(std::move(model), visible);
}

}  // namespace url_shortener::tui

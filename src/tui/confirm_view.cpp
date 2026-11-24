#include "url_shortener/tui/confirm_view.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

namespace url_shortener::tui
{

namespace
{

/// Display width of the dialog's inner content and token field, in columns.
constexpr std::size_t k_confirm_inner_width = 44;

/// The first whitespace-delimited word of @p s, used as the action-button verb
/// (e.g. `"Delete link"` -> `"Delete"`); @p s itself when it has no space.
std::string first_word(const std::string& s)
{
    const std::string::size_type space = s.find(' ');
    return space == std::string::npos ? s : s.substr(0, space);
}

/// Removes the last UTF-8 code point from @p s in place (its trailing
/// continuation bytes then its lead byte), so Backspace deletes a whole
/// character rather than a byte.
void pop_back_codepoint(std::string& s)
{
    while (!s.empty())
    {
        const auto b = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((b & 0xC0u) != 0x80u)
        {
            break;  // dropped a lead/ASCII byte; the code point is gone
        }
    }
}

}  // namespace

ftxui::Element render_confirm(const ConfirmModel& model, const Theme& theme)
{
    using namespace ftxui;

    const ConfirmRequest& req = model.request();

    Elements body;
    // The prompt already embeds the sanitized slug; sanitize again at the sink
    // for defence in depth (T-110), then wrap to the dialog width.
    body.push_back(paragraph(sanitize_for_terminal(req.prompt)));

    if (req.kind == ConfirmKind::type_token)
    {
        // Show the operator's typed input, sanitized (never the raw token).
        const std::string shown = pad_to_width(
            truncate_to_width(sanitize_for_terminal(model.input()),
                              k_confirm_inner_width),
            k_confirm_inner_width);
        body.push_back(text(shown) | border);
    }

    // Action button: dim (disabled) until the model accepts a submit, so the
    // enabled/disabled state is visible without relying on colour (mono-safe).
    const std::string action = first_word(req.title);
    Element action_btn = text("[ " + sanitize_for_terminal(action) + " ]");
    if (!model.can_submit())
    {
        action_btn = action_btn | to_decorator(theme.disabled);
    }
    body.push_back(hbox({
        std::move(action_btn),
        text("  "),
        text("[ Cancel ]"),
    }));

    Element title =
        text(" " + truncate_to_width(sanitize_for_terminal(req.title),
                                     k_confirm_inner_width) +
             " ") |
        to_decorator(theme.title);

    Element win = window(std::move(title), vbox(std::move(body))) |
                  size(WIDTH, EQUAL,
                       static_cast<int>(k_confirm_inner_width) + 4);
    return clear_under(center(std::move(win)));
}

ftxui::Component confirm_dialog(ConfirmModel& model, const Theme& theme)
{
    using namespace ftxui;

    /// The dialog component: renders the modal and handles token entry,
    /// submit (Enter, only when allowed), cancel (Esc) itself.
    class ConfirmDialogComponent final : public ComponentBase
    {
    public:
        ConfirmDialogComponent(ConfirmModel& model, const Theme& theme)
            : m_model_(model), m_theme_(theme)
        {
        }

        Element OnRender() override
        {
            if (m_model_.result().has_value())
            {
                return text("");  // decided; the dialog is gone
            }
            return render_confirm(m_model_, m_theme_);
        }

        bool OnEvent(Event event) override
        {
            if (m_model_.result().has_value())
            {
                return false;
            }
            if (event == Event::Escape)
            {
                m_model_.cancel();
                return true;
            }
            if (event == Event::Return)
            {
                m_model_.submit();  // a no-op unless can_submit()
                return true;
            }
            if (m_model_.request().kind == ConfirmKind::type_token)
            {
                if (event == Event::Backspace)
                {
                    std::string input = m_model_.input();
                    pop_back_codepoint(input);
                    m_model_.set_input(std::move(input));
                    return true;
                }
                if (event.is_character())
                {
                    // Keep the RAW typed bytes so the comparison is against the
                    // raw token, not a sanitized form (T-110).
                    m_model_.set_input(m_model_.input() + event.character());
                    return true;
                }
            }
            return true;  // modal: swallow everything else while open
        }

        bool Focusable() const override { return true; }

    private:
        ///< The dialog state to drive; owned by the caller.
        ConfirmModel& m_model_;

        ///< The active theme; owned by the caller.
        const Theme& m_theme_;
    };

    return Make<ConfirmDialogComponent>(model, theme);
}

}  // namespace url_shortener::tui

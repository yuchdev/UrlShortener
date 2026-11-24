#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/toast_view.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

namespace url_shortener::tui
{

namespace
{

/// Display width of a toast box's inner content, in columns.
constexpr std::size_t k_toast_inner_width = 40;

/// Max raw-envelope lines shown in the `[d]etails` view (defence in depth
/// against an over-tall box; the full text is still in @ref
/// Toast::raw_envelope).
constexpr std::size_t k_details_max_lines = 12;

/// Per-line byte cap applied before sanitizing a display line. A single line is
/// only ever `k_toast_inner_width` columns wide, so four bytes per column is an
/// ample bound that stops the sanitizer scanning an arbitrarily long line every
/// frame (threat model T-121).
constexpr std::size_t k_line_byte_cap = k_toast_inner_width * 4;

/// Selects the style role for a toast kind. The role carries severity through
/// attributes/text, so the mono theme (no colour) still distinguishes toasts.
const StyleSpec& style_for(const Toast& t, const Theme& theme)
{
    switch (t.kind) {
        case ToastKind::error:
            return theme.error;
        case ToastKind::success:
            return theme.ok;
        case ToastKind::info:
            break;
    }
    return theme.banner;
}

/// Splits @p text on `'\n'` into lines (a trailing newline yields no extra
/// empty line). Used so a pretty-printed raw envelope keeps its line structure
/// while each line is still sanitized before rendering (T-110).
std::vector<std::string> split_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(std::move(line));
            line.clear();
        }
        else {
            line += c;
        }
    }
    lines.push_back(std::move(line));
    return lines;
}

/// Byte-caps (T-121), sanitizes (T-110) then width-fits a single display line.
ftxui::Element safe_line(const std::string& raw)
{
    const std::string capped =
        raw.size() > k_line_byte_cap ? raw.substr(0, k_line_byte_cap) : raw;
    return ftxui::text(
        truncate_to_width(sanitize_for_terminal(capped), k_toast_inner_width));
}

}  // namespace

ftxui::Element render_toast(const Toast& t,
                            bool show_details,
                            const Theme& theme)
{
    using namespace ftxui;

    const StyleSpec& style = style_for(t, theme);

    Elements body;
    if (!t.body.empty()) {
        body.push_back(safe_line(t.body));
    }

    // Footer: "(<command>, id <id>)   [d]etails".
    std::string footer = "(";
    footer += sanitize_for_terminal(t.source_command);
    if (!t.request_id.empty()) {
        footer += ", id " + sanitize_for_terminal(t.request_id);
    }
    footer += ")";
    body.push_back(hbox({
        text(truncate_to_width(footer, k_toast_inner_width - 10))
            | to_decorator(theme.hint),
        filler(),
        text("[d]etails") | to_decorator(theme.hint),
    }));

    if (show_details) {
        body.push_back(separator());
        const std::vector<std::string> lines = split_lines(t.raw_envelope);
        std::size_t shown = 0;
        for (const std::string& line : lines) {
            if (shown++ >= k_details_max_lines) {
                body.push_back(text("..."));
                break;
            }
            body.push_back(safe_line(line));
        }
    }

    Element title = text(" "
                         + truncate_to_width(sanitize_for_terminal(t.title),
                                             k_toast_inner_width)
                         + " ")
        | to_decorator(style);

    Element win = window(std::move(title), vbox(std::move(body)));
    return win | size(WIDTH, EQUAL, static_cast<int>(k_toast_inner_width) + 4);
}

ftxui::Element render_toasts(const ToastQueue& queue,
                             bool show_details,
                             const Theme& theme)
{
    using namespace ftxui;

    const std::deque<Toast>& toasts = queue.visible();
    if (toasts.empty()) {
        return text("");
    }

    Elements boxes;
    boxes.reserve(toasts.size());
    for (std::size_t i = 0; i < toasts.size(); ++i) {
        // Details expand only the newest (back-most) toast.
        const bool details = show_details && (i + 1 == toasts.size());
        boxes.push_back(render_toast(toasts[i], details, theme));
    }

    Element stack = vbox(std::move(boxes));
    // Anchor bottom-right over the body (ADR 0003 D10 / step 2).
    return vbox({
        filler(),
        hbox({filler(), std::move(stack)}),
    });
}

ftxui::Component toast_overlay(ToastQueue& queue, const Theme& theme)
{
    using namespace ftxui;

    /// The overlay component: renders the stack and handles `d`/`Esc` only
    /// while a toast is visible; other keys flow through untouched.
    class ToastOverlayComponent final : public ComponentBase
    {
    public:
        ToastOverlayComponent(ToastQueue& queue, const Theme& theme)
            : m_queue_(queue)
            , m_theme_(theme)
        {
        }

        Element OnRender() override
        {
            if (m_queue_.visible().empty()) {
                m_details_ = false;
                return text("");
            }
            return render_toasts(m_queue_, m_details_, m_theme_);
        }

        bool OnEvent(Event event) override
        {
            if (m_queue_.visible().empty()) {
                return false;
            }
            if (event == Event::Character("d")) {
                m_details_ = !m_details_;
                return true;
            }
            if (event == Event::Escape) {
                m_queue_.dismiss_front();
                m_details_ = false;
                return true;
            }
            return false;  // let navigation flow to the screen underneath
        }

    private:
        ///< The queue to render and mutate; owned by the caller.
        ToastQueue& m_queue_;

        ///< The active theme; owned by the caller.
        const Theme& m_theme_;

        ///< Whether the raw-envelope `[d]etails` view is expanded.
        bool m_details_ = false;
    };

    return Make<ToastOverlayComponent>(queue, theme);
}

}  // namespace url_shortener::tui

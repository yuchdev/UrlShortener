/**
 * @file form_widgets.cpp
 * @brief Per-@ref url_shortener::tui::ArgKind FTXUI field widgets for the D5
 * form (subtask 06.0/08). Each widget edits its @ref FormModel field through
 * the model's mutators (keeping PATCH state authoritative) and renders a
 * **sanitized echo** of the buffer, never a raw `ftxui::Input` - so no stored
 * control / bidi / zero-width byte reaches the grid (threat model T-110). The
 * widgets build no JSON (contract C1) and name no FTXUI colour (they style only
 * through the injected @ref Theme).
 */
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

#include "url_shortener/tui/form_view.hpp"

namespace url_shortener::tui
{

namespace
{

/// Display width of a single-line text field's echoed buffer.
constexpr std::size_t k_text_inner = 40;

/// Display width of one object-member placeholder cell.
constexpr std::size_t k_member_inner = 10;

/// Sanitize (T-110) then truncate then pad @p raw to exactly @p width columns.
///
/// Only ~@p width columns are shown, so a bounded prefix of @p raw is sanitized
/// rather than the whole (up to 64 KiB) buffer: `width * 4` bytes yields at
/// least @p width display columns even for 4-byte code points, plus slack for
/// control/combining bytes the sanitizer drops (threat model T-121). The prefix
/// may split a UTF-8 sequence at its tail, but the sanitizer maps the partial
/// bytes to U+FFFD and truncation discards them, so the visible cell is
/// unaffected.
std::string fit(const std::string& raw, std::size_t width)
{
    const std::size_t bound = width * 4u + 16u;
    std::string_view view(raw);
    if (view.size() > bound) {
        view = view.substr(0, bound);
    }
    return pad_to_width(truncate_to_width(sanitize_for_terminal(view), width),
                        width);
}

/// Removes the last UTF-8 code point from @p s in place (continuation bytes
/// then the lead byte), so Backspace deletes a whole character, not a byte.
void pop_back_codepoint(std::string& s)
{
    while (!s.empty()) {
        const auto b = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((b & 0xC0u) != 0x80u) {
            break;
        }
    }
}

/// `true` when @p c is a single ASCII decimal digit.
bool is_digit_str(const std::string& c)
{
    return c.size() == 1 && c[0] >= '0' && c[0] <= '9';
}

/**
 * @brief One interactive form field widget, rendering a sanitized echo of its
 * @ref FormModel field and editing it through the model's mutators.
 *
 * Focus-aware (it highlights while @ref Focused); it consumes only its own
 * editing keys and returns `false` for Tab / Up / Down so the enclosing
 * container moves focus. An out-of-range index makes every operation inert.
 */
class FieldWidget final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the widget over @p model's field at @p index.
     * @param model The form being edited; held by reference, must outlive this.
     * @param index The field index; may be out of range (inert widget).
     * @param theme The theme for focus styling; held by reference, must outlive
     * this.
     */
    FieldWidget(FormModel& model, std::size_t index, const Theme& theme)
        : m_model_(model)
        , m_index_(index)
        , m_theme_(theme)
    {
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        if (!valid()) {
            return text("");
        }
        const FormField& f = field();
        Element e;
        switch (f.spec.kind) {
            case ArgKind::boolean:
                e = render_boolean(f);
                break;
            case ArgKind::enumeration:
                e = render_enum(f);
                break;
            case ArgKind::object:
                e = f.spec.members.empty() ? render_text(f) : render_object(f);
                break;
            default:
                e = render_text(f);
                break;
        }
        if (f.locked) {
            e = e | to_decorator(m_theme_.disabled);
        }
        return e;
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        if (!valid() || field().locked) {
            return false;
        }
        // A nullable field's visible `[x] clear` box toggles with Delete; the
        // model keeps the raw bytes, we only flip the cleared state (T-110
        // safe).
        if (field().spec.nullable && event == ftxui::Event::Delete) {
            toggle_clear();
            return true;
        }
        switch (field().spec.kind) {
            case ArgKind::boolean:
                return on_boolean(event);
            case ArgKind::enumeration:
                return on_enum(event);
            case ArgKind::object:
                return field().spec.members.empty() ? on_text(event)
                                                    : on_object(event);
            default:
                return on_text(event);
        }
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return valid() && !field().locked; }

private:
    /// Whether @ref m_index_ addresses a real field.
    bool valid() const { return m_index_ < m_model_.fields().size(); }

    /// The addressed field (only call when @ref valid).
    const FormField& field() const { return m_model_.fields()[m_index_]; }

    /// Wraps @p e in the focus highlight when this widget has focus.
    ftxui::Element focusable(ftxui::Element e) const
    {
        using namespace ftxui;
        return Focused() ? (e | inverted) : e;
    }

    // -- Renderers ---------------------------------------------------------

    /// A single-line echoed text field: `[ <sanitized, fitted buffer> ]`, or
    /// `[ (null) ]` for a cleared nullable field.
    ftxui::Element render_text(const FormField& f) const
    {
        using namespace ftxui;
        const std::string shown =
            f.state == FieldState::cleared ? std::string("(null)") : f.text;
        return focusable(text("[" + fit(shown, k_text_inner) + "]"));
    }

    /// A `[x]` / `[ ]` checkbox.
    ftxui::Element render_boolean(const FormField& f) const
    {
        using namespace ftxui;
        return focusable(text(f.checked ? "[x]" : "[ ]"));
    }

    /// A horizontal radio (`( ) a  (*) b`) for <= 3 values, or a dropdown-style
    /// `[ value            v]` for more.
    ftxui::Element render_enum(const FormField& f) const
    {
        using namespace ftxui;
        const std::vector<std::string>& values = f.spec.enum_values;
        if (values.size() <= 3) {
            std::string line;
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0) {
                    line += "  ";
                }
                const bool on = f.choice == static_cast<int>(i);
                line += on ? "(*) " : "( ) ";
                line += sanitize_for_terminal(values[i]);
            }
            return focusable(text(line));
        }
        const std::string sel =
            (f.choice >= 0 && f.choice < static_cast<int>(values.size()))
            ? values[static_cast<std::size_t>(f.choice)]
            : std::string("(choose)");
        return focusable(text("[" + fit(sel, 18) + "v]"));
    }

    /// A row of placeholder member cells (`[name] [source] ...`); the current
    /// member is bracketed with `<>` when focused.
    ftxui::Element render_object(const FormField& f) const
    {
        using namespace ftxui;
        Elements cells;
        for (std::size_t m = 0; m < f.spec.members.size(); ++m) {
            const std::string buf =
                m < f.members_text.size() ? f.members_text[m] : std::string();
            const std::string shown = buf.empty() ? f.spec.members[m] : buf;
            Element cell = text("[" + fit(shown, k_member_inner) + "]");
            if (buf.empty()) {
                cell = cell | to_decorator(m_theme_.hint);
            }
            if (Focused() && m == m_member_) {
                cell = cell | inverted;
            }
            cells.push_back(cell);
            cells.push_back(text(" "));
        }
        return hbox(std::move(cells));
    }

    // -- Event handlers ----------------------------------------------------

    /// Toggles a nullable field between cleared (emit explicit `null`) and
    /// un-cleared (back to an empty / untouched buffer per kind). The raw
    /// buffer bytes are preserved; only the cleared flag flips.
    void toggle_clear()
    {
        if (field().state == FieldState::cleared) {
            switch (field().spec.kind) {
                case ArgKind::enumeration:
                    m_model_.set_choice(m_index_, -1);
                    break;
                case ArgKind::object:
                    if (!field().spec.members.empty()) {
                        for (std::size_t m = 0; m < field().spec.members.size();
                             ++m)
                        {
                            m_model_.set_member(m_index_, m, std::string());
                        }
                        break;
                    }
                    m_model_.set_text(m_index_, std::string());
                    break;
                default:
                    m_model_.set_text(m_index_, std::string());
                    break;
            }
            return;
        }
        m_model_.clear_field(m_index_);
    }

    /// Edits a text/integer/timestamp/list/map/raw-object buffer.
    bool on_text(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Backspace) {
            std::string t = field().text;
            pop_back_codepoint(t);
            m_model_.set_text(m_index_, std::move(t));
            return true;
        }
        if (event.is_character()) {
            if (field().text.size() >= FormModel::k_max_field_bytes) {
                return true;  // at the byte cap: swallow further input (T-121)
            }
            const std::string c = event.character();
            if (field().spec.kind == ArgKind::integer) {
                const bool sign = c == "-" && field().text.empty();
                if (!is_digit_str(c) && !sign) {
                    return true;  // reject non-digit keystroke (swallowed)
                }
            }
            m_model_.set_text(m_index_, field().text + c);
            return true;
        }
        return false;
    }

    /// Toggles a boolean with Space.
    bool on_boolean(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Character(" ")) {
            m_model_.set_checked(m_index_, !field().checked);
            return true;
        }
        return false;
    }

    /// Cycles an enumeration with Left / Right / Space (Delete clears a
    /// nullable enum).
    bool on_enum(const ftxui::Event& event)
    {
        using namespace ftxui;
        const int n = static_cast<int>(field().spec.enum_values.size());
        if (n == 0) {
            return false;
        }
        if (event == Event::ArrowRight || event == Event::Character(" ")) {
            const int next = field().choice < 0 ? 0 : field().choice + 1;
            m_model_.set_choice(m_index_, next < n ? next : n - 1);
            return true;
        }
        if (event == Event::ArrowLeft) {
            m_model_.set_choice(m_index_, field().choice - 1);  // -1 unsets
            return true;
        }
        return false;
    }

    /// Edits a member-based object: Left/Right move the member cursor, keys
    /// edit the current member.
    bool on_object(const ftxui::Event& event)
    {
        using namespace ftxui;
        const std::size_t count = field().spec.members.size();
        if (count == 0) {
            return on_text(event);
        }
        if (event == Event::ArrowRight) {
            if (m_member_ + 1 < count) {
                ++m_member_;
            }
            return true;
        }
        if (event == Event::ArrowLeft) {
            if (m_member_ > 0) {
                --m_member_;
            }
            return true;
        }
        const std::string cur = m_member_ < field().members_text.size()
            ? field().members_text[m_member_]
            : std::string();
        if (event == Event::Backspace) {
            std::string t = cur;
            pop_back_codepoint(t);
            m_model_.set_member(m_index_, m_member_, std::move(t));
            return true;
        }
        if (event.is_character()) {
            if (cur.size() >= FormModel::k_max_field_bytes) {
                return true;  // at the byte cap: swallow further input (T-121)
            }
            m_model_.set_member(m_index_, m_member_, cur + event.character());
            return true;
        }
        return false;
    }

    ///< The form being edited; not owned, outlives this widget.
    FormModel& m_model_;

    ///< Index of the edited field in @ref FormModel::fields.
    std::size_t m_index_;

    ///< Theme for focus / disabled styling; not owned, outlives this widget.
    const Theme& m_theme_;

    ///< Current member cursor for a member-based object field.
    std::size_t m_member_ = 0;
};

}  // namespace

ftxui::Component make_field_widget(FormModel& model,
                                   std::size_t index,
                                   const Theme& theme)
{
    return ftxui::Make<FieldWidget>(model, index, theme);
}

}  // namespace url_shortener::tui

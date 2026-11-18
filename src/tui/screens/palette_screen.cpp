/**
 * @file palette_screen.cpp
 * @brief The Command Palette screen (D9, subtask 06.0/11): a filter box over a
 * ranked @ref url_shortener::tui::PaletteModel that opens the generic command
 * flow for the chosen command. FTXUI renderer over the model; it issues no
 * command directly (contract C8), sanitizes every command name/summary/tag
 * before width fitting (threat model T-110), and sets
 * @ref url_shortener::tui::ScreenContext::text_input_active while the filter has
 * focus so digit keys type into the query instead of switching tabs.
 */
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/screens/palette_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/palette_model.hpp>
#include <url_shortener/tui/screens/generic_command_screen.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>
#include <url_shortener/tui/toast.hpp>
#include <url_shortener/tui/toast_view.hpp>

namespace url_shortener::tui
{

namespace
{

/// The 80-column default before the body box is measured.
constexpr int k_default_width = 80;

/// Column widths for a palette row (marker + name + gap + summary + gap + tag).
constexpr std::size_t k_name_col = 16;
constexpr std::size_t k_tag_col = 16;

/// Sanitize (T-110) then truncate @p raw to at most @p width columns.
std::string fit(const std::string& raw, std::size_t width)
{
    return truncate_to_width(sanitize_for_terminal(raw), width);
}

/**
 * @brief The Command Palette body: filter input + ranked command rows, hosting
 * the generic command flow inline when a command is chosen.
 */
class PaletteComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the palette over @p context and @p commands.
     * @param context The screen context; captured by reference (owned by
     * @ref App, must outlive this).
     * @param commands The catalog to list; moved into the model.
     */
    PaletteComponent(ScreenContext& context, std::vector<CommandInfo> commands)
        : m_context_(context)
        , m_theme_(theme_for(context.options.theme))
        , m_toasts_(m_clock_)
        , m_model_(std::move(commands), context.options.read_only)
    {
        ftxui::InputOption opt;
        opt.content = &m_filter_buffer_;
        opt.multiline = false;
        m_filter_input_ = ftxui::Input(opt);
        Add(m_filter_input_);
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        if (m_command_screen_) {
            return m_command_screen_->component()->Render() | reflect(m_box_);
        }
        m_context_.text_input_active = true;  // the filter owns the keyboard
        Element base = dbox(
            {render_list(), render_toasts(m_toasts_, false, m_theme_)});
        if (m_theme_.ascii_only) {
            base = ascii_filter(std::move(base));
        }
        return base | reflect(m_box_);
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        using namespace ftxui;
        if (m_command_screen_) {
            m_command_screen_->component()->OnEvent(event);
            // The close hook only flags; the reset runs here, after the command
            // component's OnEvent has returned, so it is never destroyed while
            // its own handler is on the stack.
            if (m_close_pending_) {
                finish_close();
            }
            return true;  // the command flow is modal over the list
        }

        m_context_.text_input_active = true;
        if (event == Event::ArrowDown) {
            m_model_.select_next();
            return true;
        }
        if (event == Event::ArrowUp) {
            m_model_.select_prev();
            return true;
        }
        if (event == Event::Return) {
            activate();
            return true;
        }
        if (event == Event::Escape) {
            if (!m_filter_buffer_.empty()) {
                m_filter_buffer_.clear();
                m_model_.set_query("");
                return true;
            }
            return false;  // let App pop the palette tab
        }

        const std::string before = m_filter_buffer_;
        const bool consumed = m_filter_input_->OnEvent(event);
        if (m_filter_buffer_ != before) {
            m_model_.set_query(m_filter_buffer_);
        }
        return consumed;
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

    /// The active child drives FTXUI's focus chain (and thus which nested
    /// Container accepts keystrokes): the open command flow when one is active,
    /// otherwise the filter input. Without this the base class would keep the
    /// first focusable child (the filter) active, so a nested form's fields
    /// would never see `Focused()` and would drop every keystroke.
    ftxui::Component ActiveChild() override
    {
        if (m_command_screen_) {
            return m_command_screen_->component();
        }
        return m_filter_input_;
    }

private:
    /// Opens the generic command flow for the selected entry, or toasts the
    /// not-authorized reason for a disabled one (ADR 0003 rule 5).
    void activate()
    {
        const PaletteEntry* sel = m_model_.selected();
        if (sel == nullptr || sel->info == nullptr) {
            return;
        }
        if (!sel->enabled) {
            Toast t;
            t.kind = ToastKind::error;
            t.title = "! not_authorized";
            t.body = sel->disabled_reason;
            t.source_command = sel->info->name;
            m_toasts_.push(std::move(t));
            return;
        }
        GenericCommandConfig cfg;
        cfg.command = *sel->info;
        cfg.on_close = [this] { m_close_pending_ = true; };
        m_command_screen_ =
            make_generic_command_screen(m_context_, std::move(cfg));
        Add(m_command_screen_->component());
    }

    /// Detaches and drops the finished command screen, returning to the list.
    void finish_close()
    {
        if (m_command_screen_) {
            m_command_screen_->component()->Detach();
            m_command_screen_.reset();
        }
        m_close_pending_ = false;
        m_context_.text_input_active = true;
    }

    /// The inner content width from the last frame, or the 80-column guess.
    int measured_width() const
    {
        const int w = m_box_.x_max - m_box_.x_min + 1;
        return w > 1 ? w : k_default_width;
    }

    /// Rows of command list visible inside the window.
    std::size_t visible_rows() const
    {
        const int h = m_box_.y_max - m_box_.y_min + 1;
        const int rows = h - 5;  // borders + filter + separator
        return static_cast<std::size_t>(rows > 1 ? rows : 18);
    }

    /// The `: <query>[` filter line (D9 header).
    ftxui::Element filter_line(std::size_t content_width) const
    {
        using namespace ftxui;
        const std::string left = ": " + sanitize_for_terminal(m_filter_buffer_);
        const std::size_t budget = content_width > 3 ? content_width - 3 : 0;
        return hbox({
            text(truncate_to_width(left, budget)),
            text("["),
            filler(),
            text("]"),
        });
    }

    /// One command row: `marker name  summary  tag`, dim when disabled and
    /// inverted when selected.
    ftxui::Element entry_row(const PaletteEntry& entry,
                             bool selected,
                             std::size_t content_width) const
    {
        using namespace ftxui;
        const std::size_t fixed = 1 + k_name_col + 1 + 1 + k_tag_col;
        const std::size_t sum_col =
            content_width > fixed ? content_width - fixed : 1;
        const std::string name = entry.info->name;
        const std::string summary =
            entry.enabled ? entry.info->summary : entry.disabled_reason;

        Element row = hbox({
            text(selected ? ">" : " "),
            text(pad_to_width(fit(name, k_name_col), k_name_col)),
            text(" "),
            text(pad_to_width(fit(summary, sum_col), sum_col)),
            text(" "),
            text(pad_to_width(fit(entry.tag, k_tag_col), k_tag_col)),
        });
        if (!entry.enabled) {
            row = row | to_decorator(m_theme_.disabled);
        }
        if (selected) {
            row = row | to_decorator(m_theme_.selected_row);
        }
        return row;
    }

    /// The windowed list body.
    ftxui::Element render_list() const
    {
        using namespace ftxui;
        const std::size_t content_width =
            static_cast<std::size_t>(measured_width() > 2
                                         ? measured_width() - 2
                                         : 0);
        const std::vector<PaletteEntry>& entries = m_model_.entries();

        Elements rows;
        if (entries.empty()) {
            rows.push_back(text("  (no matching command)")
                           | to_decorator(m_theme_.hint));
        }
        else {
            const std::size_t rows_shown = visible_rows();
            const std::size_t sel = m_model_.selected_index();
            std::size_t first = 0;
            if (sel >= rows_shown) {
                first = sel - rows_shown + 1;
            }
            if (first + rows_shown > entries.size()) {
                first = entries.size() > rows_shown
                    ? entries.size() - rows_shown
                    : 0;
            }
            for (std::size_t i = first;
                 i < entries.size() && i < first + rows_shown; ++i)
            {
                rows.push_back(entry_row(entries[i], i == sel, content_width));
            }
        }

        Element content = vbox({
            filter_line(content_width),
            separator(),
            vbox(std::move(rows)) | flex,
        });
        return window(text(" Command palette ") | to_decorator(m_theme_.title),
                      std::move(content));
    }

    ///< Screen context (caller, session, router, options); not owned.
    ScreenContext& m_context_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< Wall clock backing the toast queue expiry.
    SystemClock m_clock_;

    ///< Toasts (e.g. the not-authorized notice) rendered over the list.
    ToastQueue m_toasts_;

    ///< The ranking model over the catalog.
    PaletteModel m_model_;

    ///< The filter input buffer (the leading `:` is implicit).
    std::string m_filter_buffer_;

    ///< The filter text input component.
    ftxui::Component m_filter_input_;

    ///< The active generic command flow, or null while listing.
    std::unique_ptr<IScreen> m_command_screen_;

    ///< `true` when the command flow asked to close; the reset is deferred to a
    ///< safe point in @ref OnEvent.
    bool m_close_pending_ = false;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;
};

/**
 * @brief The Command Palette screen (D9): wraps @ref PaletteComponent as an
 * @ref IScreen.
 */
class PaletteScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context and @p commands.
     * @param context The screen context; captured by the component by reference.
     * @param commands The catalog to list; moved in.
     */
    PaletteScreen(ScreenContext& context, std::vector<CommandInfo> commands)
        : m_component_(
              ftxui::Make<PaletteComponent>(context, std::move(commands)))
    {
    }

    ScreenId id() const override { return ScreenId::palette; }

    ftxui::Component component() override { return m_component_; }

    std::vector<KeyHint> hints() const override
    {
        return {KeyHint {"type", "filter"},
                KeyHint {"Up/Down", "move"},
                KeyHint {"Enter", "run"},
                KeyHint {"Esc", "close"}};
    }

private:
    ///< The palette body component.
    ftxui::Component m_component_;
};

}  // namespace

std::unique_ptr<IScreen> make_palette_screen(ScreenContext& context,
                                             std::vector<CommandInfo> commands)
{
    return std::make_unique<PaletteScreen>(context, std::move(commands));
}

}  // namespace url_shortener::tui

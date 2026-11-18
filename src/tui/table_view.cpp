#include "url_shortener/tui/table_view.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

namespace url_shortener::tui
{

namespace
{

/// Width guess used for the very first frame, before @ref ftxui::reflect has
/// measured the real box; the 80x24 minimum the frame guarantees (D1). The
/// goldens render twice so this guess never reaches the committed output.
constexpr int k_default_width = 80;

/// Columns reserved on the left of every row for the selection marker (`>`).
constexpr int k_marker_width = 1;

/**
 * @brief Sanitize then width-fit one untrusted string into exactly @p width
 * display columns (the single T-110 choke point for the table body).
 *
 * The order is mandatory: @ref sanitize_for_terminal runs *first* so a later
 * truncation can never re-expose half of a stripped escape; the result is then
 * truncated (ellipsis on overflow) and padded to @p width.
 *
 * @param raw The untrusted cell / header / title text.
 * @param width The target display width in columns.
 * @param right_align Right-justify (pad on the left) when `true`.
 * @return Printable, single-line text of display width @p width.
 */
std::string fit_cell(const std::string& raw, int width, bool right_align)
{
    const auto w = width < 0 ? std::size_t {0} : static_cast<std::size_t>(width);
    return pad_to_width(truncate_to_width(sanitize_for_terminal(raw), w), w,
                        right_align);
}

/// The horizontal-overflow marker for the footer: `<` when columns are hidden
/// to the left, `>` when hidden to the right, around the word `more`. Empty
/// when nothing is scrolled off either edge.
std::string overflow_marker(bool left, bool right)
{
    if (!left && !right)
    {
        return std::string();
    }
    std::string out;
    if (left)
    {
        out += "< ";
    }
    out += "more";
    if (right)
    {
        out += " >";
    }
    return out;
}

/**
 * @brief The reusable table body of D3 / D6 / D8 (subtask 06.0/10).
 *
 * Renders a @ref TableModel's current page and routes keys to it. Owns the
 * filter input buffer, the column-chooser modal and the reflected body width;
 * holds no copy of the data (always re-reads the model, so rendering is a pure
 * function of model state).
 */
class TableViewComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the view over @p model with @p options.
     *
     * @param model The model to render and drive; held by reference, must
     * outlive this component.
     * @param options Presentation and callbacks; moved in.
     */
    TableViewComponent(TableModel& model, TableViewOptions options)
        : m_model_(model), m_options_(std::move(options))
    {
        using namespace ftxui;
        InputOption input_option;
        input_option.content = &m_filter_buffer_;
        input_option.multiline = false;
        m_filter_input_ = Input(input_option);

        m_chooser_ = make_column_chooser(m_model_, &m_chooser_open_,
                                         m_options_.theme);
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;

        const int width = measured_width();
        const int data_width = width - k_marker_width;

        Elements lines;
        lines.push_back(title_line());
        if (m_options_.show_filter_bar)
        {
            lines.push_back(filter_line());
        }
        lines.push_back(header_line(data_width));
        append_body(lines, data_width);
        lines.push_back(filler());
        lines.push_back(footer_line(data_width));

        Element base = vbox(std::move(lines)) | reflect(m_box_);
        if (m_chooser_open_)
        {
            return dbox({std::move(base), m_chooser_->Render()});
        }
        return base;
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        using namespace ftxui;

        // 1. Column chooser, while open, is modal.
        if (m_chooser_open_)
        {
            m_chooser_->OnEvent(event);
            return true;
        }

        // 2. Filter input, while focused, owns the keyboard (nav keys become
        //    text; Enter commits one query, Esc clears and returns focus).
        if (m_filter_focused_)
        {
            if (event == Event::Escape)
            {
                m_filter_buffer_.clear();
                m_filter_focused_ = false;
                m_model_.set_filter(std::string());
                return true;
            }
            if (event == Event::Return)
            {
                m_filter_focused_ = false;
                m_model_.set_filter(m_filter_buffer_);
                return true;
            }
            m_filter_input_->OnEvent(event);
            return true;
        }

        // 3. Screen-declared extra keys win over the built-ins (so `s` can mean
        //    "stats" in D3 while meaning "sort" in D6).
        if (m_options_.on_extra_key &&
            m_options_.on_extra_key(event, m_model_.selected()))
        {
            return true;
        }

        // 4. Built-in navigation.
        return handle_builtin(event);
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

private:
    /// The current body width: the reflected box from the previous frame, or
    /// the 80-column guess on the first frame.
    int measured_width() const
    {
        const int w = m_box_.x_max - m_box_.x_min + 1;
        return w > 1 ? w : k_default_width;
    }

    /// The active theme, or the dark fallback when none was supplied.
    const Theme& theme() const
    {
        return m_options_.theme != nullptr ? *m_options_.theme
                                           : theme_for(ThemeName::dark);
    }

    /// The title line: sanitized title on the left, optional screen extra on
    /// the right.
    ftxui::Element title_line() const
    {
        using namespace ftxui;
        Elements cells;
        cells.push_back(
            text(sanitize_for_terminal(m_options_.title)) | bold);
        cells.push_back(filler());
        if (m_options_.header_extra)
        {
            cells.push_back(m_options_.header_extra());
        }
        return hbox(std::move(cells));
    }

    /// The filter bar: `filter: [<buffer>]`, buffer sanitized for display.
    ftxui::Element filter_line() const
    {
        using namespace ftxui;
        const std::string shown =
            sanitize_for_terminal(m_filter_buffer_);
        Element field = text("[" + shown + "]");
        if (m_filter_focused_)
        {
            field = field | inverted;
        }
        return hbox({text("filter: ") | to_decorator(theme().hint),
                     std::move(field)});
    }

    /// The header row: a blank marker cell then each shown column's bold title.
    ftxui::Element header_line(int data_width) const
    {
        using namespace ftxui;
        const std::vector<int> widths = m_model_.layout(data_width);
        const std::vector<const ColumnDef*> cols =
            m_model_.shown_columns(data_width);

        Elements cells;
        cells.push_back(text(std::string(k_marker_width, ' ')));
        for (std::size_t i = 0; i < cols.size(); ++i)
        {
            cells.push_back(
                text(fit_cell(cols[i]->title, widths[i], cols[i]->right_align)));
            if (i + 1 < cols.size())
            {
                cells.push_back(text(" "));
            }
        }
        return hbox(std::move(cells)) | to_decorator(theme().header_row);
    }

    /// Appends the data rows, or the loading / empty / error placeholder, into
    /// @p lines.
    void append_body(ftxui::Elements& lines, int data_width) const
    {
        using namespace ftxui;

        const bool have_rows = !m_model_.rows().empty();
        if (!have_rows)
        {
            switch (m_model_.state())
            {
                case LoadState::loading:
                case LoadState::idle:
                    lines.push_back(text("loading...") | dim);
                    return;
                case LoadState::empty:
                case LoadState::loaded:
                    lines.push_back(text("no rows") | dim);
                    return;
                case LoadState::error:
                    lines.push_back(
                        text("! " + sanitize_for_terminal(
                                        m_model_.error_message())) |
                        to_decorator(theme().error));
                    lines.push_back(text("r retry") |
                                    to_decorator(theme().hint));
                    return;
            }
        }

        const std::vector<int> widths = m_model_.layout(data_width);
        const std::vector<const ColumnDef*> cols =
            m_model_.shown_columns(data_width);
        const std::vector<ColumnDef>& all = m_model_.columns();
        const std::vector<std::vector<std::string>>& rows = m_model_.rows();
        const std::optional<std::size_t> sel = m_model_.selected();
        const bool stale = m_model_.state() == LoadState::error;

        for (std::size_t r = 0; r < rows.size(); ++r)
        {
            const bool selected = sel.has_value() && *sel == r;
            Elements cells;
            cells.push_back(text(selected ? ">" : " "));
            for (std::size_t i = 0; i < cols.size(); ++i)
            {
                const auto idx =
                    static_cast<std::size_t>(cols[i] - all.data());
                const std::string& raw =
                    idx < rows[r].size() ? rows[r][idx] : k_empty_cell;
                cells.push_back(
                    text(fit_cell(raw, widths[i], cols[i]->right_align)));
                if (i + 1 < cols.size())
                {
                    cells.push_back(text(" "));
                }
            }
            Element line = hbox(std::move(cells));
            if (selected)
            {
                line = line | to_decorator(theme().selected_row);
            }
            else if (stale)
            {
                line = line | dim;
            }
            lines.push_back(std::move(line));
        }
    }

    /// The footer: `page N/M  (K per page)` on the left, the overflow marker on
    /// the right.
    ftxui::Element footer_line(int data_width) const
    {
        using namespace ftxui;
        const std::string marker = overflow_marker(
            m_model_.overflow_left(data_width),
            m_model_.overflow_right(data_width));
        Elements cells;
        cells.push_back(text(page_footer(m_model_)) |
                        to_decorator(theme().hint));
        cells.push_back(filler());
        if (!marker.empty())
        {
            cells.push_back(text(marker) | to_decorator(theme().hint));
        }
        return hbox(std::move(cells));
    }

    /// Handles the built-in navigation keys; returns `true` when consumed.
    bool handle_builtin(const ftxui::Event& event)
    {
        using namespace ftxui;

        if (m_options_.show_filter_bar && event == Event::Character("/"))
        {
            m_filter_focused_ = true;
            return true;
        }
        if (event == Event::Character("c"))
        {
            m_chooser_open_ = true;
            return true;
        }
        if (event == Event::Character("j") || event == Event::ArrowDown)
        {
            m_model_.select_next();
            return true;
        }
        if (event == Event::Character("k") || event == Event::ArrowUp)
        {
            m_model_.select_prev();
            return true;
        }
        if (event == Event::Character("g"))
        {
            m_model_.select_first();
            return true;
        }
        if (event == Event::Character("G"))
        {
            m_model_.select_last();
            return true;
        }
        if (event == Event::PageDown)
        {
            m_model_.next_page();
            return true;
        }
        if (event == Event::PageUp)
        {
            m_model_.prev_page();
            return true;
        }
        if (event == Event::Home)
        {
            m_model_.first_page();
            return true;
        }
        if (event == Event::End)
        {
            m_model_.last_page();
            return true;
        }
        if (event == Event::Character("<"))
        {
            m_model_.scroll_columns(-1);
            return true;
        }
        if (event == Event::Character(">"))
        {
            m_model_.scroll_columns(1);
            return true;
        }
        if (m_options_.sort_key_enabled && event == Event::Character("s"))
        {
            toggle_sort_current();
            return true;
        }
        if (event == Event::Character("r"))
        {
            m_model_.refresh();
            return true;
        }
        if (event == Event::Return)
        {
            if (m_options_.on_activate && m_model_.selected().has_value())
            {
                m_options_.on_activate(*m_model_.selected());
            }
            return true;
        }
        return false;
    }

    /// Sorts by the currently sorted column, or the first sortable column.
    void toggle_sort_current()
    {
        const std::vector<ColumnDef>& cols = m_model_.columns();
        const std::string& active = m_model_.query().sort_key;
        std::optional<std::size_t> target;
        for (std::size_t i = 0; i < cols.size(); ++i)
        {
            if (!active.empty() && cols[i].key == active)
            {
                target = i;
                break;
            }
            if (!target.has_value() && cols[i].sortable)
            {
                target = i;
            }
        }
        if (target.has_value())
        {
            m_model_.toggle_sort(*target);
        }
    }

    ///< The `-` placeholder for a cell whose column is wider than the row.
    static inline const std::string k_empty_cell = "-";

    ///< Model rendered and driven; must outlive this component.
    TableModel& m_model_;

    ///< Presentation and callbacks (owned copy).
    TableViewOptions m_options_;

    ///< Current filter text, bound to @ref m_filter_input_.
    std::string m_filter_buffer_;

    ///< Hidden input used only to edit @ref m_filter_buffer_ while focused.
    ftxui::Component m_filter_input_;

    ///< `true` while the filter input owns the keyboard.
    bool m_filter_focused_ = false;

    ///< The column-chooser modal component.
    ftxui::Component m_chooser_;

    ///< `true` while the column chooser is open (shared with the chooser).
    bool m_chooser_open_ = false;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;
};

}  // namespace

std::string page_footer(const TableModel& model)
{
    return "page " + std::to_string(model.page_index() + 1) + "/" +
           std::to_string(model.page_count()) + "  (" +
           std::to_string(model.query().limit) + " per page)";
}

ftxui::Component make_table_view(TableModel& model, TableViewOptions options)
{
    return ftxui::Make<TableViewComponent>(model, std::move(options));
}

}  // namespace url_shortener::tui

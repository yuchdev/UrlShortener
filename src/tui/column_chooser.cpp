#include "url_shortener/tui/table_view.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

namespace url_shortener::tui
{

namespace
{

/// Maximum display width of a column title in the chooser (keeps the modal
/// narrow and bounds a hostile title, T-110).
constexpr std::size_t k_chooser_label_width = 32;

/**
 * @brief The `c` column-chooser modal: a vertical list of checkboxes, one per
 * table column, toggling @ref TableModel visibility.
 *
 * Each checkbox is bound to an owned `bool` initialised from the column's
 * current visibility. On change the component calls
 * @ref TableModel::toggle_column and re-reads the model so a refused hide (the
 * last visible column) snaps the checkbox back. Enter or Esc closes the modal.
 */
class ColumnChooserComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the chooser over @p model.
     *
     * @param model Model whose columns are toggled; held by reference and must
     * outlive this component.
     * @param open Non-owning "is open" flag cleared on close; must outlive this.
     * @param theme Theme for styling; `nullptr` falls back to dark. Non-owning.
     */
    ColumnChooserComponent(TableModel& model, bool* open, const Theme* theme)
        : m_model_(model), m_open_(open), m_theme_(theme)
    {
        using namespace ftxui;

        const std::vector<ColumnDef>& cols = m_model_.columns();
        m_checked_.reserve(cols.size());
        m_labels_.reserve(cols.size());

        Components boxes;
        for (std::size_t i = 0; i < cols.size(); ++i)
        {
            m_checked_.push_back(std::make_unique<bool>(cols[i].visible));
            m_labels_.push_back(truncate_to_width(
                sanitize_for_terminal(cols[i].title), k_chooser_label_width));

            CheckboxOption option;
            option.label = m_labels_[i];
            option.checked = m_checked_[i].get();
            option.on_change = [this, i] { on_toggle(i); };
            boxes.push_back(Checkbox(option));
        }

        m_container_ = Container::Vertical(std::move(boxes));
        Add(m_container_);
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        if (m_open_ == nullptr || !*m_open_)
        {
            return text("");
        }
        const Theme& theme =
            m_theme_ != nullptr ? *m_theme_ : theme_for(ThemeName::dark);
        Element body = m_container_->Render();
        Element win = window(text("Columns") | to_decorator(theme.header_row),
                             std::move(body));
        return clear_under(center(std::move(win)));
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        using namespace ftxui;
        if (m_open_ == nullptr || !*m_open_)
        {
            return false;
        }
        if (event == Event::Escape || event == Event::Return)
        {
            *m_open_ = false;
            return true;
        }
        m_container_->OnEvent(event);
        return true;  // modal: swallow every key while open
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override
    {
        return m_open_ != nullptr && *m_open_;
    }

private:
    /// Applies checkbox @p i to the model, then re-syncs the checkbox to the
    /// model's (possibly refused) visibility.
    void on_toggle(std::size_t i)
    {
        m_model_.toggle_column(i);
        if (i < m_model_.columns().size() && i < m_checked_.size())
        {
            *m_checked_[i] = m_model_.columns()[i].visible;
        }
    }

    ///< Model whose column visibility this chooser edits; must outlive this.
    TableModel& m_model_;

    ///< Non-owning open flag cleared on close; must outlive this.
    bool* m_open_;

    ///< Theme for styling; may be `nullptr` (dark fallback). Non-owning.
    const Theme* m_theme_;

    ///< One owned flag per column, bound to its checkbox.
    std::vector<std::unique_ptr<bool>> m_checked_;

    ///< Sanitized, width-fitted checkbox labels (own the strings the checkboxes
    ///< reference).
    std::vector<std::string> m_labels_;

    ///< The vertical container of checkboxes.
    ftxui::Component m_container_;
};

}  // namespace

ftxui::Component
make_column_chooser(TableModel& model, bool* open, const Theme* theme)
{
    return ftxui::Make<ColumnChooserComponent>(model, open, theme);
}

}  // namespace url_shortener::tui

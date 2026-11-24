#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/table_model.hpp>
#include <url_shortener/tui/theme.hpp>

namespace url_shortener::tui
{

/**
 * @file table_view.hpp
 * @brief The reusable FTXUI table body of D3 (Links), D6 (Tables) and D8
 * (Jobs): the one renderer and key router over a @ref TableModel (ADR 0003
 * Decision 2, subtask 06.0/10).
 *
 * The view is a *thin* renderer: it owns no paging, sort or filter logic (that
 * is the @ref TableModel's) and no command knowledge (columns and key meaning
 * come from the owning screen through @ref TableViewOptions). Every server /
 * stored cell, header, title, status and error string is passed through
 * @ref sanitize_for_terminal exactly once, in a single place in the view,
 * *before* width fitting, so no untrusted control, bidi or zero-width byte can
 * reach FTXUI `text()` (threat model T-110, CRITICAL). Rendering is a pure
 * function of @ref TableModel state, so goldens are reproducible.
 */

/**
 * @brief Per-view presentation and callback options the owning screen supplies.
 *
 * A value type the screen fills before calling @ref make_table_view. The view
 * copies it and holds it for its lifetime; any captured reference inside the
 * @ref on_activate / @ref on_extra_key / @ref header_extra callables must
 * outlive the returned component (the screen owns both).
 */
struct TableViewOptions
{
    ///< Title shown on the table's header line (e.g. `"Links"`,
    ///< `"links  (1,204 rows)"`); composition text, still sanitized before use.
    std::string title;

    ///< Active theme; styles the header, selected row, hints and error text.
    ///< Non-owning; must outlive the component. When `nullptr` the view falls
    ///< back to the dark theme.
    const Theme* theme = nullptr;

    ///< `true` to show the `/`-driven filter bar above the table (Links,
    ///< Tables).
    bool show_filter_bar = true;

    ///< `true` when the built-in `s` key means "sort the sorted column"; Links
    ///< sets it `false` so `s` is free for its "stats" action (handled through
    ///< @ref on_extra_key). The footer hint follows this flag.
    bool sort_key_enabled = true;

    ///< Called with the selected row index when Enter is pressed on a row;
    ///< empty when the screen has no detail action. Never called with no
    ///< selection.
    std::function<void(std::size_t row_index)> on_activate;

    ///< Screen-specific key handler consulted *before* the built-in navigation
    ///< for every key (so a screen can claim `n`/`e`/`d`/`s`/...). It receives
    ///< the event and the currently selected row (or `nullopt`) and returns
    ///< `true` to consume the key; returning `false` lets the built-ins run.
    std::function<bool(const ftxui::Event&, std::optional<std::size_t> row)>
        on_extra_key;

    ///< Extra screen hints merged into the footer before the built-in ones.
    std::vector<KeyHint> extra_hints;

    ///< Optional element rendered on the right of the title line (e.g. the D3
    ///< status radio); empty when unused. **Trusted-caller boundary:** the view
    ///< does not sanitize this element's text, so the screen must build it only
    ///< from composition / already-sanitized strings, never raw server data
    ///< (T-110).
    std::function<ftxui::Element()> header_extra;

    ///< Called whenever the filter input gains (`true`) or loses (`false`)
    ///< focus, so the owning screen can mirror it into
    ///< `ScreenContext::text_input_active` and stop the global hotkeys (digits
    ///< /
    ///< `:` / `q`) from firing while the operator types a filter. Empty when
    ///< the screen does not track text-input focus.
    std::function<void(bool focused)> on_text_input_changed;
};

/**
 * @brief Builds the interactive table-view component over @p model.
 *
 * The returned component renders @p model's current page and routes keys in
 * this order (subtask 06.0/10 "Event routing order"): the column chooser (while
 * open) -> the filter input (while focused; Esc/Enter return focus) ->
 * @ref TableViewOptions::on_extra_key -> the built-in navigation
 * (`j`/`k`/arrows/PgUp/PgDn/`g`/`G`/Home/End/`<`/`>`/`s`/`c`/`r`/Enter). While
 * the filter input has focus the navigation keys are treated as text and do not
 * move the selection. The component never performs I/O: paging keys ask the
 * model to emit a @ref PageQuery, which the owning screen runs off the UI
 * thread.
 *
 * @param model The table model to render and drive; a reference held for the
 * component's lifetime. The caller owns it and must keep it alive at least as
 * long as the component.
 * @param options The presentation and callback options; copied into the
 * component.
 * @return A never-null FTXUI component suitable for placing in a screen body.
 */
ftxui::Component make_table_view(TableModel& model, TableViewOptions options);

/**
 * @brief Builds the `c` column-chooser modal: one @ref ftxui::Checkbox per
 * column, bound to @p model's visibility, closed on Enter/Esc.
 *
 * Toggling a checkbox calls @ref TableModel::toggle_column, which refuses to
 * hide the last visible column; the checkbox state is re-synced to the model so
 * a refused toggle visibly snaps back. Rendering and labels are sanitized
 * (T-110), though column titles are composition text.
 *
 * @param model The model whose columns are toggled; held by reference and must
 * outlive the component.
 * @param open Non-owning flag set to `false` when the chooser closes (Enter or
 * Esc); owned by the view, must outlive the component.
 * @param theme The theme used to style the modal; `nullptr` falls back to dark.
 * Non-owning; must outlive the component.
 * @return A never-null component rendering the checkbox list in a titled box.
 */
ftxui::Component make_column_chooser(TableModel& model,
                                     bool* open,
                                     const Theme* theme);

/**
 * @brief The footer page string for @p model, e.g. `"page 1/61  (20 per
 * page)"`.
 *
 * A pure, FTXUI-free string derived only from the model's page index, page
 * count and page size, so it can be unit-tested without a terminal.
 *
 * @param model The model to describe.
 * @return `"page <index+1>/<count>  (<limit> per page)"`.
 */
std::string page_footer(const TableModel& model);

}  // namespace url_shortener::tui

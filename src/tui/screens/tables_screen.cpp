/**
 * @file tables_screen.cpp
 * @brief The Database tables browser screen (D6, subtask 06.0/14): a two-pane
 * view with a left list of logical tables (row counts + the read-only note) and
 * a right paged, sortable, searchable `TableView` over `db.rows`. It renders a
 * selected row's exact JSON inline through a @ref ResultViewerModel (D11 edge
 * `Tables -> RowJson`) and hosts the generated `link.create` form inline for
 * insert on a `writable` table only (D11 edge `Tables -> LinkForm`).
 *
 * Every server/stored cell renders through the reusable `TableView` sanitizer
 * and the row-JSON view through `ResultViewerModel`, so no raw ESC/CSI/OSC/C1/
 * bidi byte reaches the terminal (threat model T-110). The one mutating submit
 * (`link.create`) routes through the form's @ref run_mutation choke point with
 * the session read-only flag (ADR 0003 rule 4, threat model T-116). The screen
 * builds no JSON itself (the @ref TablesModel does, via `rows_request`,
 * contract C1/C9), never tokenizes JSON (`json_view` only, contract C9), issues
 * no command through @ref ICommandClient (contract C8), and sends only
 * `db.tables`, `db.rows` and `link.create` (success criterion 3). Every async
 * call carries a cancellable token and an epoch so a stale response cannot
 * overwrite newer state or open a view after the operator has moved on; all are
 * cancelled on leave.
 */
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/screens/tables_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/form_model.hpp>
#include <url_shortener/tui/form_view.hpp>
#include <url_shortener/tui/result_viewer_model.hpp>
#include <url_shortener/tui/row_mapper.hpp>
#include <url_shortener/tui/table_model.hpp>
#include <url_shortener/tui/table_view.hpp>
#include <url_shortener/tui/tables_model.hpp>
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

/// Outer width (including the border) of the left pane.
constexpr int k_left_pane_width = 30;

/// Refresh `db.tables` on re-entering the tab only when older than this.
constexpr std::chrono::seconds k_stale_after {30};

/// Sanitize (T-110) then truncate @p raw to at most @p width columns.
std::string fit(const std::string& raw, std::size_t width)
{
    return truncate_to_width(sanitize_for_terminal(raw), width);
}

/// Greedily word-wraps @p text into lines of at most @p width columns (used for
/// the left-pane note); each word is itself truncated when it alone overflows.
std::vector<std::string> wrap_words(const std::string& text, std::size_t width)
{
    std::vector<std::string> lines;
    if (width == 0) {
        return lines;
    }
    std::string current;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t space = text.find(' ', start);
        const std::size_t end =
            space == std::string::npos ? text.size() : space;
        const std::string word = text.substr(start, end - start);
        if (!word.empty()) {
            if (current.empty()) {
                current = word;
            }
            else if (current.size() + 1 + word.size() <= width) {
                current += " ";
                current += word;
            }
            else {
                lines.push_back(current);
                current = word;
            }
        }
        if (space == std::string::npos) {
            break;
        }
        start = space + 1;
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

/**
 * @brief The Database tables browser body (D6, subtask 06.0/14).
 *
 * Lifetime: this top-level screen component lives for the whole of `App::run`
 * (the `Container::Tab` keeps every screen alive; tab switches never destroy
 * it), so a completion that captures `this` is safe for the whole session. It
 * needs no separate per-screen alive guard: every async call routes through the
 * injected @ref AsyncCaller (its submits via the member @ref MutationFlow,
 * whose own `m_alive_` covers an inline insert form torn down mid-session), and
 * `App::run` declares the `LoopRuntime` - hence the caller - before the
 * screens, so the caller is destroyed AFTER them: at shutdown `AsyncCaller`'s
 * own `m_alive_` marks it dead and joins its workers, so a call in flight at
 * teardown is dropped, never delivered to a destroyed screen (T-120). If this
 * screen ever adds a poll timer or posts directly through @ref IUiPoster
 * (bypassing the caller's completion guard), that handler MUST capture a
 * `std::weak_ptr` alive-guard, as JobsComponent and DashboardComponent do.
 */
class TablesComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the component over @p context and @p catalog.
     *
     * @param context The screen context; its caller/router/options are owned by
     * @ref App and captured by reference, so they must outlive this component.
     * @param catalog The command catalog, used to resolve the `link.create`
     * descriptor the insert form needs; moved in.
     */
    TablesComponent(ScreenContext& context, std::vector<CommandInfo> catalog)
        : m_context_(context)
        , m_catalog_(std::move(catalog))
        , m_theme_(theme_for(context.options.theme))
        , m_toasts_(m_clock_)
        , m_policy_(context.options.assume_yes)
    {
    }

    /// Refreshes `db.tables` when first shown or when the data is stale.
    void on_enter_refresh()
    {
        if (!m_tables_loaded_
            || (m_clock_.now() - m_last_loaded_) > k_stale_after)
        {
            load_tables();
        }
    }

    /// Called when the tab is left: drop every in-flight call so a late
    /// response cannot touch a backgrounded screen (stale-response discipline).
    void on_leave_cleanup()
    {
        cancel_tables();
        cancel_rows();
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        if (m_form_close_pending_) {
            finish_form_close();
        }
        m_toasts_.tick();

        Element body;
        if (m_mode_ == Mode::form && m_form_) {
            body = m_form_->Render();
        }
        else if (m_mode_ == Mode::row_json && m_row_view_) {
            body = render_row_json();
        }
        else {
            body = render_two_pane();
        }
        body =
            dbox({std::move(body), render_toasts(m_toasts_, false, m_theme_)});
        if (m_theme_.ascii_only) {
            body = ascii_filter(std::move(body));
        }
        return body | reflect(m_box_);
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        using namespace ftxui;
        if (m_form_close_pending_) {
            finish_form_close();
        }
        if (m_mode_ == Mode::row_json) {
            return row_json_keys(event);
        }
        if (m_mode_ == Mode::form && m_form_) {
            const bool consumed = m_form_->OnEvent(event);
            if (m_form_close_pending_) {
                finish_form_close();
            }
            return consumed;
        }

        // Tables mode. Tab swaps panes unless a text input (the filter) owns
        // the keyboard; a focused filter receives every key.
        if (event == Event::Tab && !m_context_.text_input_active) {
            toggle_pane();
            return true;
        }
        if (m_context_.text_input_active && m_rows_view_) {
            return m_rows_view_->OnEvent(event);
        }
        if (m_pane_ == Pane::left) {
            return left_pane_keys(event);
        }
        if (m_rows_view_) {
            return m_rows_view_->OnEvent(event);
        }
        return false;
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

    /// The active child drives FTXUI's focus chain: the open form while
    /// inserting, otherwise the rows table view (when one exists).
    ftxui::Component ActiveChild() override
    {
        if (m_mode_ == Mode::form && m_form_) {
            return m_form_;
        }
        if (m_rows_view_) {
            return m_rows_view_;
        }
        return ComponentBase::ActiveChild();
    }

private:
    ///< The three screen states: the two-pane browser, the row-JSON viewer, the
    ///< inline insert form.
    enum class Mode
    {
        tables,
        row_json,
        form
    };

    ///< Which pane currently owns navigation keys.
    enum class Pane
    {
        left,
        right
    };

    // --- db.tables -----------------------------------------------------------

    /// Submits one `db.tables`, superseding any in-flight one.
    void load_tables()
    {
        cancel_tables();
        const std::uint64_t epoch = m_tables_epoch_;
        commands::CommandRequest request;
        request.v = 1;
        request.id = m_ids_.next();
        request.command = "db.tables";
        request.args_json = "{}";
        m_tables_token_ = m_context_.caller.submit(
            std::move(request),
            [this, epoch](const commands::CommandResponse& response)
            { on_tables_response(response, epoch); });
    }

    /// Applies a `db.tables` response: on success populate the left pane and
    /// load the selected table's first page; on failure raise an error toast.
    void on_tables_response(const commands::CommandResponse& response,
                            std::uint64_t epoch)
    {
        if (epoch != m_tables_epoch_) {
            return;  // superseded or the tab was left
        }
        m_tables_token_ = 0;
        if (!response.ok) {
            m_toasts_.push(
                toast_from_response("db.tables", response.id, response));
            return;
        }
        m_tables_.apply_tables(response.result_json);
        m_tables_loaded_ = true;
        m_last_loaded_ = m_clock_.now();
        if (m_tables_.selected().has_value()) {
            build_rows_for_selected();
        }
    }

    /// Cancels any in-flight `db.tables` and bumps the epoch so a late
    /// completion is dropped.
    void cancel_tables()
    {
        if (m_tables_token_ != 0) {
            m_context_.caller.cancel(m_tables_token_);
            m_tables_token_ = 0;
        }
        ++m_tables_epoch_;
    }

    // --- Table selection / db.rows -------------------------------------------

    /// Moves the left-pane selection by @p delta rows and, when it changes,
    /// loads the newly selected table's first page.
    void move_table_selection(int delta)
    {
        const std::optional<std::size_t> sel = m_tables_.selected();
        if (!sel.has_value()) {
            return;
        }
        std::size_t index = *sel;
        if (delta < 0) {
            const std::size_t step = static_cast<std::size_t>(-delta);
            index = index > step ? index - step : 0;
        }
        else {
            index += static_cast<std::size_t>(delta);
        }
        if (index == *sel) {
            return;
        }
        m_tables_.select(index);
        if (m_tables_.selected() != sel) {
            build_rows_for_selected();
        }
    }

    /// Builds a fresh rows table model + view for the selected table (resetting
    /// sort/filter) and loads its first page.
    void build_rows_for_selected()
    {
        cancel_rows();
        std::vector<ColumnDef> columns = m_tables_.columns_for_selected();
        m_rows_keys_ = m_tables_.column_keys_for_selected();
        m_rows_model_ = std::make_unique<TableModel>(std::move(columns),
                                                     TablesModel::k_page_size);
        m_rows_model_->set_query_handler(
            [this](const PageQuery& query, std::uint64_t generation)
            { run_rows(query, generation); });

        if (m_rows_view_) {
            m_rows_view_->Detach();
        }
        m_rows_view_ = make_table_view(*m_rows_model_, make_rows_options());
        Add(m_rows_view_);
        m_rows_model_->refresh();  // emits db.rows page 1
    }

    /// Builds the table-view options for the current rows model.
    TableViewOptions make_rows_options()
    {
        TableViewOptions options;
        options.title = right_pane_title();
        options.theme = &m_theme_;
        options.show_filter_bar = selected_has_searchable_column();
        // `s` is handled here (cycle across sortable columns), so the view's
        // built-in sort key is off.
        options.sort_key_enabled = false;
        options.on_activate = [this](std::size_t row) { open_row_json(row); };
        options.on_extra_key = [this](const ftxui::Event& event,
                                      std::optional<std::size_t>) -> bool
        { return handle_rows_key(event); };
        options.on_text_input_changed = [this](bool focused)
        { m_context_.text_input_active = focused; };
        options.header_extra = [this] { return sort_indicator(); };
        return options;
    }

    /// Submits one `db.rows` for the selected table and @p query, superseding
    /// any in-flight one; a stale response (wrong epoch) is dropped.
    void run_rows(const PageQuery& query, std::uint64_t generation)
    {
        if (m_rows_token_ != 0) {
            m_context_.caller.cancel(m_rows_token_);
        }
        const std::uint64_t epoch = m_rows_epoch_;
        m_rows_token_ = m_context_.caller.submit(
            m_tables_.rows_request(query, m_ids_.next()),
            [this, generation, epoch](const commands::CommandResponse& response)
            { on_rows_response(response, generation, epoch); });
    }

    /// Applies a `db.rows` response: map rows on success; on failure keep the
    /// previous rows, raise a toast, and reload the left pane for a `not_found`
    /// (the table vanished).
    void on_rows_response(const commands::CommandResponse& response,
                          std::uint64_t generation,
                          std::uint64_t epoch)
    {
        if (epoch != m_rows_epoch_ || !m_rows_model_) {
            return;  // the table changed or the tab was left
        }
        m_rows_token_ = 0;
        if (!response.ok) {
            m_rows_model_->apply_error(generation, response.error.message);
            m_toasts_.push(
                toast_from_response("db.rows", response.id, response));
            if (response.error.code == commands::CommandErrorCode::not_found) {
                load_tables();  // left pane reloads (step 6)
            }
            return;
        }
        std::optional<PageResult> page =
            rows_from_result(response.result_json, m_rows_keys_, "rows");
        if (!page.has_value()) {
            m_rows_model_->apply_error(generation, "malformed db.rows result");
            return;
        }
        m_rows_model_->apply_result(generation, std::move(*page));
    }

    /// Cancels any in-flight `db.rows` and bumps the epoch so a late completion
    /// is dropped (e.g. after a table switch or on leave).
    void cancel_rows()
    {
        if (m_rows_token_ != 0) {
            m_context_.caller.cancel(m_rows_token_);
            m_rows_token_ = 0;
        }
        ++m_rows_epoch_;
    }

    // --- Right-pane keys -----------------------------------------------------

    /// The screen-specific keys the table view defers to: `s` sort, `n` insert.
    bool handle_rows_key(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Character("s")) {
            cycle_sort();
            return true;
        }
        if (event == Event::Character("n")) {
            start_insert();
            return true;
        }
        return false;
    }

    /// Cycles the sort across the visible sortable columns: no sort -> first
    /// sortable ascending -> descending -> next sortable column ascending ...
    void cycle_sort()
    {
        if (!m_rows_model_) {
            return;
        }
        const std::vector<ColumnDef>& cols = m_rows_model_->columns();
        std::vector<std::size_t> sortable;
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (cols[i].sortable && cols[i].visible) {
                sortable.push_back(i);
            }
        }
        if (sortable.empty()) {
            push_info("no sortable column in this table");
            return;
        }
        const std::string& active = m_rows_model_->query().sort_key;
        std::optional<std::size_t> position;
        for (std::size_t p = 0; p < sortable.size(); ++p) {
            if (!active.empty() && cols[sortable[p]].key == active) {
                position = p;
                break;
            }
        }
        if (!position.has_value()) {
            m_rows_model_->toggle_sort(sortable[0]);  // -> ascending
        }
        else if (!m_rows_model_->query().descending) {
            m_rows_model_->toggle_sort(sortable[*position]);  // asc -> desc
        }
        else {
            const std::size_t next = (*position + 1) % sortable.size();
            m_rows_model_->toggle_sort(
                sortable[next]);  // next column ascending
        }
    }

    // --- Row JSON ------------------------------------------------------------

    /// Opens the row-JSON viewer over the selected row's stored JSON,
    /// projecting out any secret-named top-level field first so the viewer
    /// stays symmetric with the right-pane column filter (security review
    /// 06.0/14 LOW).
    void open_row_json(std::size_t /*row*/)
    {
        if (!m_rows_model_) {
            return;
        }
        const std::string* raw = m_rows_model_->selected_raw();
        if (raw == nullptr) {
            return;
        }
        const std::string redacted =
            TablesModel::redact_secret_fields(*raw, &m_row_hidden_);
        m_row_view_ =
            std::make_unique<ResultViewerModel>(redacted, view_width());
        m_mode_ = Mode::row_json;
        // The viewer is modal; claim the keyboard so Esc/scroll reach here.
        m_context_.text_input_active = true;
    }

    /// Closes the row-JSON viewer and returns to the two-pane browser.
    void close_row_json()
    {
        m_row_view_.reset();
        m_mode_ = Mode::tables;
        m_context_.text_input_active = false;
    }

    /// Row-JSON viewer keys: scroll, Esc back (modal).
    bool row_json_keys(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Escape) {
            close_row_json();
            return true;
        }
        if (m_row_view_) {
            if (event == Event::Character("j") || event == Event::ArrowDown) {
                m_row_view_->scroll(1);
            }
            else if (event == Event::Character("k") || event == Event::ArrowUp)
            {
                m_row_view_->scroll(-1);
            }
            else if (event == Event::PageDown) {
                m_row_view_->page(1);
            }
            else if (event == Event::PageUp) {
                m_row_view_->page(-1);
            }
        }
        return true;  // the viewer is modal
    }

    // --- Insert form ---------------------------------------------------------

    /// The insert command for a writable table; empty when none maps. Only
    /// `links` is writable in v1, mapping to `link.create` (documented
    /// coupling, ADR 0003 gap: `db.tables` has no insert-command hint).
    static std::string insert_command_for(const std::string& table)
    {
        if (table == "links") {
            return "link.create";
        }
        return std::string();
    }

    /// Looks up a command in the catalog; `nullptr` when absent.
    const CommandInfo* find_command(const std::string& name) const
    {
        for (const CommandInfo& info : m_catalog_) {
            if (info.name == name) {
                return &info;
            }
        }
        return nullptr;
    }

    /// Opens the `link.create` form for the selected writable table (step 4).
    void start_insert()
    {
        // The one capability decision (subtask 18): insert is link.create (a
        // write), so a read-only session is refused here with the reason.
        CommandInfo create_probe;
        create_probe.name = "link.create";
        create_probe.permission = "write";
        const Capability cap = capability_for(m_context_).can_run(create_probe);
        if (!cap.allowed) {
            toast_unavailable(cap.reason, "link.create");
            return;
        }
        if (!m_tables_.can_insert()) {
            toast_unavailable("table is read-only", "link.create");
            return;
        }
        const std::optional<std::size_t> sel = m_tables_.selected();
        if (!sel.has_value()) {
            return;
        }
        const std::string table = m_tables_.tables()[*sel].name;
        const std::string command = insert_command_for(table);
        if (command.empty()) {
            toast_unavailable("insert is not supported here", "insert");
            return;
        }
        const CommandInfo* info = find_command(command);
        if (info == nullptr) {
            toast_unavailable("command unavailable", command);
            return;
        }
        open_form(*info, "New row in " + table);
    }

    /// Builds the generated form for @p info and shows it inline.
    void open_form(const CommandInfo& info, const std::string& title)
    {
        m_form_model_ = std::make_unique<FormModel>(info);
        FormViewContext view_ctx {
            m_context_.caller,
            m_policy_,
            m_toasts_,
            m_theme_,
            capability_for(m_context_),
            title,
            [this](const commands::CommandResponse& response)
            { on_form_success(response); },
            [this] { m_form_close_pending_ = true; },
            [this](bool focused) { m_context_.text_input_active = focused; },
        };
        m_form_ctx_ = std::make_unique<FormViewContext>(std::move(view_ctx));
        m_form_ = make_form_component(*m_form_model_, *m_form_ctx_);
        Add(m_form_);
        m_mode_ = Mode::form;
        m_context_.text_input_active = true;
    }

    /// After a successful insert: close the form and refresh the rows plus the
    /// `db.tables` counts (the form's flow already raised the success toast).
    void on_form_success(const commands::CommandResponse& response)
    {
        if (!response.ok || response.job.has_value()) {
            return;  // keep the form open; the flow toasted the outcome
        }
        m_form_close_pending_ = true;
        if (m_rows_model_) {
            m_rows_model_->refresh();
        }
        load_tables();  // row counts changed
    }

    /// Detaches and drops the finished form, returning to the browser.
    void finish_form_close()
    {
        if (m_form_) {
            m_form_->Detach();
            m_form_.reset();
        }
        m_form_ctx_.reset();
        m_form_model_.reset();
        m_form_close_pending_ = false;
        m_mode_ = Mode::tables;
        m_context_.text_input_active = false;
    }

    // --- Pane focus ----------------------------------------------------------

    /// Switches the focused pane (left <-> right).
    void toggle_pane()
    {
        m_pane_ = m_pane_ == Pane::left ? Pane::right : Pane::left;
    }

    /// Left-pane navigation keys; returns `true` when one is consumed.
    /// Unhandled keys return `false` so the global hotkeys (tabs / palette /
    /// quit) still fire.
    bool left_pane_keys(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::ArrowDown || event == Event::Character("j")) {
            move_table_selection(1);
            return true;
        }
        if (event == Event::ArrowUp || event == Event::Character("k")) {
            move_table_selection(-1);
            return true;
        }
        if (event == Event::Return) {
            if (m_tables_.selected().has_value()) {
                m_pane_ = Pane::right;  // focus the rows after choosing a table
            }
            return true;
        }
        return false;
    }

    // --- Rendering -----------------------------------------------------------

    /// The whole two-pane body: left table list + right rows table.
    ftxui::Element render_two_pane()
    {
        using namespace ftxui;
        Element left = render_left_pane();
        Element right = m_rows_view_
            ? m_rows_view_->Render()
            : (text("select a table") | to_decorator(m_theme_.hint));
        Elements right_cells;
        right_cells.push_back(std::move(right) | flex);
        if (m_rows_model_) {
            const std::size_t hidden = m_tables_.hidden_secret_columns();
            if (hidden != 0) {
                right_cells.push_back(
                    text(fit(std::to_string(hidden)
                                 + " column(s) hidden (secret-like name)",
                             right_width()))
                    | to_decorator(m_theme_.hint));
            }
            right_cells.push_back(action_hints_line());
        }
        Element right_col = vbox(std::move(right_cells)) | flex;
        return hbox({std::move(left) | size(WIDTH, EQUAL, k_left_pane_width),
                     std::move(right_col)});
    }

    /// The left pane: a titled list of tables with counts, plus the fixed D6
    /// note at the bottom.
    ftxui::Element render_left_pane()
    {
        using namespace ftxui;
        const std::size_t inner =
            static_cast<std::size_t>(k_left_pane_width - 2);
        Elements rows;
        if (!m_tables_loaded_) {
            rows.push_back(text("loading...") | to_decorator(m_theme_.hint));
        }
        else if (m_tables_.tables().empty()) {
            rows.push_back(text("no tables") | to_decorator(m_theme_.hint));
        }
        else {
            const std::optional<std::size_t> sel = m_tables_.selected();
            for (std::size_t i = 0; i < m_tables_.tables().size(); ++i) {
                Element line = text(fit(m_tables_.left_pane_row(i), inner));
                if (sel.has_value() && *sel == i) {
                    line = line | to_decorator(m_theme_.selected_row);
                }
                rows.push_back(std::move(line));
            }
        }
        Elements note_lines;
        for (const std::string& line :
             wrap_words(TablesModel::note_text(), inner))
        {
            note_lines.push_back(text(fit(line, inner))
                                 | to_decorator(m_theme_.hint));
        }
        Element content = vbox({
            vbox(std::move(rows)) | flex,
            separator(),
            vbox(std::move(note_lines)),
        });
        return window(text(" Tables ") | to_decorator(m_theme_.title),
                      std::move(content));
    }

    /// The right-pane sort indicator shown on the title line (trusted-caller
    /// boundary: the column name is sanitized here before it reaches the view).
    ftxui::Element sort_indicator() const
    {
        using namespace ftxui;
        if (!m_rows_model_) {
            return text("");
        }
        const PageQuery& query = m_rows_model_->query();
        if (query.sort_key.empty()) {
            return text("");
        }
        const std::string arrow = query.descending ? "v" : "^";
        const std::string label =
            "sort: " + fit(query.sort_key, 20) + " " + arrow;
        return text(label) | to_decorator(m_theme_.hint);
    }

    /// The in-body action-key hint line; write keys dim in a read-only session.
    ftxui::Element action_hints_line() const
    {
        using namespace ftxui;
        Elements cells;
        const auto hint = [&](const char* label)
        { cells.push_back(text(label) | to_decorator(m_theme_.hint)); };
        hint("Enter row JSON");
        if (selected_has_searchable_column()) {
            cells.push_back(text("  "));
            hint("/ search");
        }
        cells.push_back(text("  "));
        hint("s sort");
        cells.push_back(text("  "));
        hint("c cols");
        if (m_tables_.can_insert()) {
            cells.push_back(text("  "));
            // Insert is link.create (a write); dim its hint when the capability
            // denies writes (subtask 18).
            CommandInfo create_probe;
            create_probe.name = "link.create";
            create_probe.permission = "write";
            const bool insert_disabled =
                !capability_for(m_context_).can_run(create_probe).allowed;
            Element insert = text("n insert");
            insert = insert
                | to_decorator(insert_disabled ? m_theme_.disabled
                                               : m_theme_.hint);
            cells.push_back(std::move(insert));
        }
        cells.push_back(text("  "));
        hint("Tab panes");
        return hbox(std::move(cells));
    }

    /// The scrollable row-JSON viewer body.
    ftxui::Element render_row_json()
    {
        using namespace ftxui;
        const std::size_t width = view_width();
        Elements lines;
        for (const std::string& line :
             m_row_view_->visible_lines(view_height()))
        {
            lines.push_back(text(truncate_to_width(line, width)));
        }
        if (lines.empty()) {
            lines.push_back(text("(empty)") | to_decorator(m_theme_.hint));
        }
        if (m_row_hidden_ != 0) {
            // Defence-in-depth notice, symmetric with the column filter: a
            // secret-named field was projected out before display.
            lines.push_back(text(fit(std::to_string(m_row_hidden_)
                                         + " secret-named field(s) hidden",
                                     width))
                            | to_decorator(m_theme_.hint));
        }
        std::string footer = "j/k scroll  Esc back";
        if (m_row_view_->truncated()) {
            footer += "   (truncated)";
        }
        Element content = vbox({
            vbox(std::move(lines)) | flex,
            separator(),
            text(footer) | to_decorator(m_theme_.hint),
        });
        return window(text(" row JSON ") | to_decorator(m_theme_.title),
                      std::move(content));
    }

    /// The right-pane title: `links  (1,204 rows)` for the selected table.
    std::string right_pane_title() const
    {
        const std::optional<std::size_t> sel = m_tables_.selected();
        if (!sel.has_value()) {
            return std::string();
        }
        const TableInfo& info = m_tables_.tables()[*sel];
        return info.name + "  (" + group_thousands(info.rows) + " rows)";
    }

    /// Whether the selected table has at least one searchable column (gates the
    /// `/` filter bar).
    bool selected_has_searchable_column() const
    {
        const std::optional<std::size_t> sel = m_tables_.selected();
        if (!sel.has_value()) {
            return false;
        }
        for (const TableColumnInfo& column : m_tables_.tables()[*sel].columns) {
            if (column.searchable
                && !TablesModel::is_secret_column_name(column.name))
            {
                return true;
            }
        }
        return false;
    }

    /// Thousands grouping for the title (mirrors the model's formatting).
    static std::string group_thousands(std::uint64_t value)
    {
        const std::string digits = std::to_string(value);
        std::string out;
        const std::size_t first =
            digits.size() % 3 == 0 ? 3 : digits.size() % 3;
        for (std::size_t i = 0; i < digits.size(); ++i) {
            if (i >= first && (i - first) % 3 == 0) {
                out += ',';
            }
            out += digits[i];
        }
        return out;
    }

    // --- Measurement ---------------------------------------------------------

    /// The inner content width from the last frame, or the 80-column guess.
    int measured_width() const
    {
        const int width = m_box_.x_max - m_box_.x_min + 1;
        return width > 1 ? width : k_default_width;
    }

    /// The right-pane content width (after the left pane and its border).
    std::size_t right_width() const
    {
        const int width = measured_width() - k_left_pane_width - 2;
        return static_cast<std::size_t>(width > 8 ? width : 40);
    }

    /// Wrapping width handed to the row-JSON viewer (inside the window border).
    std::size_t view_width() const
    {
        const int width = measured_width() - 4;
        return static_cast<std::size_t>(width > 8 ? width : 72);
    }

    /// Rows of text visible inside the row-JSON viewer window.
    std::size_t view_height() const
    {
        const int height = m_box_.y_max - m_box_.y_min + 1;
        const int rows = height - 4;
        return static_cast<std::size_t>(rows > 1 ? rows : 18);
    }

    // --- Toasts --------------------------------------------------------------

    /// Raises the fixed "not available" notice for a refused action.
    void toast_unavailable(const std::string& reason,
                           const std::string& command)
    {
        Toast toast;
        toast.kind = ToastKind::error;
        toast.title = "! not_authorized";
        toast.body = reason;
        toast.source_command = command;
        m_toasts_.push(std::move(toast));
    }

    /// Raises a neutral informational notice.
    void push_info(const std::string& body)
    {
        Toast toast;
        toast.kind = ToastKind::info;
        toast.title = "note";
        toast.body = body;
        m_toasts_.push(std::move(toast));
    }

    ///< Screen context (caller, session, router, options); not owned.
    ScreenContext& m_context_;

    ///< Command catalog used to resolve the `link.create` insert descriptor.
    std::vector<CommandInfo> m_catalog_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< Wall clock backing the toast queue and the staleness check.
    SystemClock m_clock_;

    ///< Toasts pushed by the actions and the submit flow.
    ToastQueue m_toasts_;

    ///< Confirm policy for the insert form (honors `--yes`).
    ConfirmPolicy m_policy_;

    ///< The D6 metadata model: table list, selection, column derivation.
    TablesModel m_tables_;

    ///< Source of correlation ids for every request this screen makes.
    RequestIdGenerator m_ids_;

    ///< The rebuilt-per-table paged model over the selected table's columns.
    std::unique_ptr<TableModel> m_rows_model_;

    ///< The reusable table view over @ref m_rows_model_.
    ftxui::Component m_rows_view_;

    ///< The row field keys the current rows model reads, parallel to its
    ///< columns (for @ref rows_from_result).
    std::vector<std::string> m_rows_keys_;

    ///< Token of the in-flight `db.tables` call; `0` when none is pending.
    CallToken m_tables_token_ = 0;

    ///< Token of the in-flight `db.rows` call; `0` when none is pending.
    CallToken m_rows_token_ = 0;

    ///< Monotonic tag for `db.tables` submissions; a completion whose captured
    ///< epoch no longer matches is dropped (stale-response guard).
    std::uint64_t m_tables_epoch_ = 0;

    ///< Monotonic tag for `db.rows` submissions; bumped on every table switch
    ///< and on leave so a response for the old table/model is dropped.
    std::uint64_t m_rows_epoch_ = 0;

    ///< `true` once a `db.tables` page has loaded (drives the stale check).
    bool m_tables_loaded_ = false;

    ///< When the last successful `db.tables` loaded; compared with
    ///< @ref k_stale_after.
    std::chrono::system_clock::time_point m_last_loaded_;

    ///< Current screen state.
    Mode m_mode_ = Mode::tables;

    ///< Which pane currently owns navigation keys.
    Pane m_pane_ = Pane::left;

    ///< The row-JSON viewer while @ref Mode::row_json.
    std::unique_ptr<ResultViewerModel> m_row_view_;

    ///< Number of secret-named fields projected out of the open row's JSON;
    ///< drives the one-line "N secret-named field(s) hidden" viewer note.
    std::size_t m_row_hidden_ = 0;

    ///< The insert form model while @ref Mode::form; heap-held so the view
    ///< context's reference stays stable.
    std::unique_ptr<FormModel> m_form_model_;

    ///< The insert form view context; heap-held so its reference members stay
    ///< stable.
    std::unique_ptr<FormViewContext> m_form_ctx_;

    ///< The generated insert form component while @ref Mode::form.
    ftxui::Component m_form_;

    ///< `true` when the form asked to close; the reset is deferred to a safe
    ///< point in @ref OnEvent / @ref OnRender.
    bool m_form_close_pending_ = false;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;
};

/**
 * @brief The Database tables browser screen (D6 node): wraps
 * @ref TablesComponent as an @ref IScreen and forwards the active-screen
 * lifecycle to it.
 *
 * The D6 action keys are rendered in-body, so @ref hints returns empty to avoid
 * duplicating them in the global footer.
 */
class TablesScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context and @p catalog.
     * @param context The screen context; captured by the component by
     * reference.
     * @param catalog The command catalog; moved in.
     */
    TablesScreen(ScreenContext& context, std::vector<CommandInfo> catalog)
        : m_impl_(ftxui::Make<TablesComponent>(context, std::move(catalog)))
    {
    }

    ScreenId id() const override { return ScreenId::tables; }

    ftxui::Component component() override { return m_impl_; }

    std::vector<KeyHint> hints() const override { return {}; }

    void on_enter() override { m_impl_->on_enter_refresh(); }

    void on_leave() override { m_impl_->on_leave_cleanup(); }

private:
    ///< The Tables body component (kept as the derived type so @ref on_enter
    ///< can reach @ref TablesComponent::on_enter_refresh).
    std::shared_ptr<TablesComponent> m_impl_;
};

}  // namespace

std::unique_ptr<IScreen> make_tables_screen(ScreenContext& context,
                                            std::vector<CommandInfo> commands)
{
    return std::make_unique<TablesScreen>(context, std::move(commands));
}

}  // namespace url_shortener::tui

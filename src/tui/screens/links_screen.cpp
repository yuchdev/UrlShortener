/**
 * @file links_screen.cpp
 * @brief The Links tab screen (D3, subtask 06.0/12): a bespoke convenience over
 * the generic `link.*` calls. It drives a @ref TableModel through
 * @ref AsyncCaller with `build_link_list_request`, renders the D3 filter bar,
 * status radio and table through the reusable table view, hosts the generated
 * `link.create` / `link.update` form inline for new/edit, hands a selected link
 * to the inline Link detail screen (D4, subtask 06.0/13) on `Enter` / `s`, and
 * shows a `link.preview` result in a scrollable viewer.
 *
 * Every mutating submit (toggle / delete / restore / create / update) routes
 * through @ref run_mutation with the session read-only flag and the shared
 * @ref ConfirmPolicy decision (ADR 0003 rule 4, threat model T-116); the
 * confirm choice is taken from the submitted `{"slug":...}` args, never a
 * command-name list in this file (review 2). The screen issues no command
 * through @ref ICommandClient directly (contract C8), builds no JSON itself
 * (the model library does, via `build_link_list_request` /
 * `build_slug_request`, contract C1/C9), and sanitizes every server/stored
 * string before display (threat model T-110). The status radio in
 * `header_extra` is composed only from fixed labels, so the trusted-caller
 * boundary carries no server data (review 6).
 */
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/screens/links_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/form_model.hpp>
#include <url_shortener/tui/form_view.hpp>
#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/links_model.hpp>
#include <url_shortener/tui/links_query.hpp>
#include <url_shortener/tui/mutation_flow.hpp>
#include <url_shortener/tui/result_viewer_model.hpp>
#include <url_shortener/tui/row_mapper.hpp>
#include <url_shortener/tui/screens/link_detail_screen.hpp>
#include <url_shortener/tui/table_model.hpp>
#include <url_shortener/tui/table_view.hpp>
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

/// Refresh the page on re-entering the tab only when the data is older than
/// this (implementation step 5).
constexpr std::chrono::seconds k_stale_after {30};

/// The one place `link.list` items are mapped: the array field name.
constexpr char k_items_field[] = "items";

/**
 * @brief The Links tab body: the D3 table plus the inline new/edit form and the
 * link detail / preview viewer (subtask 06.0/12).
 *
 * Lifetime: this top-level screen component lives for the whole of `App::run`
 * (the `Container::Tab` keeps every screen alive; tab switches never destroy
 * it), so a completion that captures `this` is safe for the whole session. It
 * needs no separate per-screen alive guard: every async call routes through the
 * injected @ref AsyncCaller (its submits via the member @ref MutationFlow,
 * whose own `m_alive_` drops a late completion when an inline detail/edit
 * sub-screen is torn down), and `App::run` declares the `LoopRuntime` - hence
 * the caller - before the screens, so the caller is destroyed AFTER them: at
 * shutdown `AsyncCaller`'s own `m_alive_` marks it dead and joins its workers,
 * so a call in flight at teardown is dropped, never delivered to a destroyed
 * screen (T-120). If this screen ever adds a poll timer or posts directly
 * through
 * @ref IUiPoster (bypassing the caller's completion guard), that handler MUST
 * capture a `std::weak_ptr` alive-guard, as JobsComponent and
 * DashboardComponent do.
 */
class LinksComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the component over @p context and @p catalog.
     *
     * @param context The screen context; its caller/router are references owned
     * by @ref App that must outlive this component (captured by reference).
     * @param catalog The command catalog, used to resolve the `link.*`
     * @ref CommandInfo each mutation needs; moved in.
     */
    LinksComponent(ScreenContext& context, std::vector<CommandInfo> catalog)
        : m_context_(context)
        , m_catalog_(std::move(catalog))
        , m_theme_(theme_for(context.options.theme))
        , m_toasts_(m_clock_)
        , m_policy_(context.options.assume_yes)
        , m_flow_(MutationDeps {context.caller,
                                m_policy_,
                                m_toasts_,
                                m_theme_,
                                capability_for(context)})
        , m_model_(capability_for(context))
        , m_table_(m_model_.columns(), 20)
    {
        using namespace ftxui;
        m_table_.set_query_handler(
            [this](const PageQuery& query, std::uint64_t generation)
            { run_list(query, generation); });

        TableViewOptions options;
        options.title = "Links";
        options.theme = &m_theme_;
        options.show_filter_bar = true;
        // `s` is the "stats" action, so the view's built-in sort key is off.
        options.sort_key_enabled = false;
        options.on_activate = [this](std::size_t) { open_detail_selected(); };
        options.on_extra_key =
            [this](const Event& event, std::optional<std::size_t> row)
        { return handle_action_key(event, row); };
        options.header_extra = [this] { return status_radio(); };
        options.on_text_input_changed = [this](bool focused)
        { m_context_.text_input_active = focused; };
        m_table_view_ = make_table_view(m_table_, std::move(options));
        Add(m_table_view_);

        // Review A (display): the `all` / `deleted` views raise link.list to a
        // write permission (D16), so a read-only session cannot query them.
        // Default to `active` rather than issuing a request the server would
        // refuse (the all/deleted radios render disabled below).
        if (!status_queryable(StatusFilter::all)) {
            m_status_ = StatusFilter::active;
        }
    }

    /// Whether this session may query the given status view - the one
    /// capability decision (subtask 18), arg-aware: `status=all|deleted` raises
    /// link.list to a write permission (D16), so a read-only session cannot run
    /// them.
    bool status_queryable(StatusFilter status) const
    {
        CommandInfo list;
        list.name = "link.list";
        list.permission = "read";
        const std::string args =
            "{\"status\":\"" + status_to_wire(status) + "\"}";
        return capability_for(m_context_).can_run(list, args).allowed;
    }

    /// Refreshes the page when first shown or when the data is stale (step 5).
    void on_enter_refresh()
    {
        if (!m_loaded_ || (m_clock_.now() - m_last_loaded_) > k_stale_after) {
            refresh();
        }
    }

    /// Called when the tab is left: drop any in-flight preview so its result
    /// cannot open a viewer behind another tab, and tear down an open detail
    /// screen so re-entering the tab lands on the table (its in-flight calls
    /// are cancelled by @ref finish_detail_close).
    ///
    /// An open new/edit FORM (@ref Mode::form) is deliberately NOT closed here:
    /// half-entered form input must survive a tab switch so the operator can
    /// consult another tab (e.g. Tables) and return to the still-populated
    /// form, rather than losing their typing. Only the read-only preview and
    /// the detail viewer - which hold in-flight reads or a transient result and
    /// have no unsaved input - are cleaned up. A regression test
    /// (`form_survives_leave_enter_while_detail_closed`) pins this.
    void on_leave_cleanup()
    {
        cancel_preview();
        if (m_mode_ == Mode::detail) {
            finish_detail_close();
        }
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        if (m_form_close_pending_) {
            finish_form_close();
        }
        if (m_detail_close_pending_) {
            finish_detail_close();
        }
        m_toasts_.tick();

        Element body;
        if (m_mode_ == Mode::detail && m_detail_component_) {
            return m_detail_component_->Render() | reflect(m_box_);
        }
        if (m_mode_ == Mode::form && m_form_) {
            body = m_form_->Render();
        }
        else if (m_mode_ == Mode::result && m_result_) {
            body = render_result();
        }
        else {
            body = render_table();
            if (m_flow_.awaiting_confirm()) {
                body = dbox({std::move(body), m_flow_.render_overlay()});
            }
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
        if (m_form_close_pending_) {
            finish_form_close();
        }
        if (m_detail_close_pending_) {
            finish_detail_close();
        }
        if (m_mode_ == Mode::detail && m_detail_component_) {
            const bool consumed = m_detail_component_->OnEvent(event);
            if (m_detail_close_pending_) {
                finish_detail_close();
            }
            return consumed;
        }
        if (m_mode_ == Mode::result) {
            return result_keys(event);
        }
        if (m_mode_ == Mode::form && m_form_) {
            const bool consumed = m_form_->OnEvent(event);
            if (m_form_close_pending_) {
                finish_form_close();
            }
            return consumed;
        }
        // Table mode. A bespoke mutation's confirm dialog is modal over the
        // table; while it is open the screen owns the keyboard so a digit in a
        // typed-slug confirm cannot switch tabs (threat model T-116, review 4).
        if (m_flow_.awaiting_confirm()) {
            const bool handled = m_flow_.overlay_event(event);
            m_context_.text_input_active = m_flow_.awaiting_confirm();
            return handled;
        }
        // Any key in table mode means the operator moved on, so an in-flight
        // preview must not open its viewer late (over a changed selection or a
        // started action). A fresh `p` re-submits through do_preview.
        if (m_preview_token_ != 0) {
            cancel_preview();
        }
        const bool consumed = m_table_view_->OnEvent(event);
        if (m_flow_.awaiting_confirm()) {
            m_context_.text_input_active = true;
        }
        return consumed;
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

    /// The active child drives FTXUI's focus chain: the open form when editing,
    /// otherwise the table view.
    ftxui::Component ActiveChild() override
    {
        if (m_mode_ == Mode::detail && m_detail_component_) {
            return m_detail_component_;
        }
        if (m_mode_ == Mode::form && m_form_) {
            return m_form_;
        }
        return m_table_view_;
    }

private:
    ///< The four screen states: the table, the new/edit form, the preview
    ///< viewer, and the inline Link detail screen (D4, subtask 06.0/13).
    enum class Mode
    {
        table,
        form,
        result,
        detail
    };

    // --- Data loading --------------------------------------------------------

    /// Re-emits the current query (the `r` refresh and the post-mutation
    /// refresh).
    void refresh() { m_table_.refresh(); }

    /// The table model's query handler: builds and submits one `link.list`.
    void run_list(const PageQuery& query, std::uint64_t generation)
    {
        const LinkListFilter filter =
            parse_filter_text(query.filter, m_status_);
        commands::CommandRequest request =
            build_link_list_request(filter, query, m_ids_.next());
        if (m_list_token_ != 0) {
            m_context_.caller.cancel(m_list_token_);
        }
        m_list_token_ = m_context_.caller.submit(
            std::move(request),
            [this, generation](const commands::CommandResponse& response)
            { on_list_response(response, generation); });
    }

    /// Applies a `link.list` response: maps rows on success, keeps the previous
    /// rows and raises an error toast on failure (so the table is never
    /// blanked).
    void on_list_response(const commands::CommandResponse& response,
                          std::uint64_t generation)
    {
        m_list_token_ = 0;
        if (!response.ok) {
            m_table_.apply_error(generation, response.error.message);
            m_toasts_.push(
                toast_from_response("link.list", response.id, response));
            return;
        }
        std::optional<PageResult> page = rows_from_result(
            response.result_json, m_model_.column_keys(), k_items_field);
        if (!page.has_value()) {
            m_table_.apply_error(generation, "malformed link.list result");
            return;
        }
        if (m_table_.apply_result(generation, std::move(*page))) {
            m_loaded_ = true;
            m_last_loaded_ = m_clock_.now();
        }
    }

    // --- Selection helpers ---------------------------------------------------

    /// The selected link's `status` string, or empty when nothing is selected.
    std::string selected_status() const
    {
        const std::string* raw = m_table_.selected_raw();
        if (raw == nullptr) {
            return std::string();
        }
        return get_string(*raw, "status").value_or(std::string());
    }

    /// The selected link's `slug`, or empty when nothing is selected.
    std::string selected_slug() const
    {
        const std::string* raw = m_table_.selected_raw();
        if (raw == nullptr) {
            return std::string();
        }
        return get_string(*raw, "slug").value_or(std::string());
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

    // --- Action keys ---------------------------------------------------------

    /// Routes the D3 action keys; returns `true` when one is consumed.
    bool handle_action_key(const ftxui::Event& event,
                           std::optional<std::size_t> /*row*/)
    {
        using namespace ftxui;
        if (event == Event::Tab) {
            cycle_status();
            return true;
        }
        if (event == Event::Character("n")) {
            start_create();
            return true;
        }
        if (event == Event::Character("e")) {
            start_edit();
            return true;
        }
        if (event == Event::Character("t")) {
            do_toggle();
            return true;
        }
        if (event == Event::Character("d")) {
            do_mutation(LinkAction::remove, "link.delete");
            return true;
        }
        if (event == Event::Character("u")) {
            do_mutation(LinkAction::restore, "link.restore");
            return true;
        }
        if (event == Event::Character("p")) {
            do_preview();
            return true;
        }
        if (event == Event::Character("s")) {
            open_detail_selected();
            return true;
        }
        return false;
    }

    /// The next status in cyclic radio order.
    static StatusFilter next_status(StatusFilter status)
    {
        switch (status) {
            case StatusFilter::all:
                return StatusFilter::active;
            case StatusFilter::active:
                return StatusFilter::disabled;
            case StatusFilter::disabled:
                return StatusFilter::deleted;
            case StatusFilter::deleted:
                return StatusFilter::all;
        }
        return StatusFilter::active;
    }

    /// Cycles the status radio and reloads page 1 with the current filter text.
    ///
    /// Review A (display): a status the session cannot query (all/deleted in a
    /// read-only session) is skipped, so Tab never lands on a disabled choice.
    void cycle_status()
    {
        StatusFilter candidate = next_status(m_status_);
        for (int i = 0; i < 4 && !status_queryable(candidate); ++i) {
            candidate = next_status(candidate);
        }
        m_status_ = candidate;
        // set_filter resets the offset to page 1 and re-queries; the handler
        // reads the new m_status_.
        m_table_.set_filter(m_table_.query().filter);
    }

    /// Enable/disable the selected link after a confirm.
    void do_toggle()
    {
        const std::string status = selected_status();
        const ActionAvailability avail =
            m_model_.availability(LinkAction::toggle, status);
        if (!avail.enabled) {
            toast_unavailable(avail.reason, "link.toggle");
            return;
        }
        const std::string command = m_model_.toggle_command(status);
        if (command.empty()) {
            toast_unavailable("cannot toggle this link", "link.toggle");
            return;
        }
        submit_mutation(command);
    }

    /// Delete / restore (and the shared availability gate) for a row action.
    void do_mutation(LinkAction action, const std::string& command)
    {
        const ActionAvailability avail =
            m_model_.availability(action, selected_status());
        if (!avail.enabled) {
            toast_unavailable(avail.reason, command);
            return;
        }
        submit_mutation(command);
    }

    /// Builds the `{"slug":...}` request and routes it through the mutation
    /// choke point (confirm + read-only + submit); refreshes once on success.
    void submit_mutation(const std::string& command)
    {
        const std::string slug = selected_slug();
        if (slug.empty()) {
            toast_unavailable("select a link first", command);
            return;
        }
        const CommandInfo* info = find_command(command);
        if (info == nullptr) {
            toast_unavailable("command unavailable", command);
            return;
        }
        run_mutation(m_flow_,
                     build_slug_request(command, slug, m_ids_.next()),
                     *info,
                     [this](const commands::CommandResponse& response)
                     {
                         if (response.ok) {
                             refresh();
                         }
                     });
        if (m_flow_.awaiting_confirm()) {
            m_context_.text_input_active = true;
        }
    }

    /// Submits `link.preview` for the selected link and shows it in the viewer.
    ///
    /// Applies the same stale-response discipline as @ref run_list: any
    /// in-flight preview is cancelled first and the new call is tagged with the
    /// current @ref m_preview_epoch_, so a slow response cannot open the viewer
    /// after the operator has moved on (changed selection, started another
    /// preview/detail/mutation, or left the viewer).
    void do_preview()
    {
        const std::string slug = selected_slug();
        if (slug.empty()) {
            toast_unavailable("select a link first", "link.preview");
            return;
        }
        cancel_preview();
        const std::uint64_t epoch = m_preview_epoch_;
        m_preview_token_ = m_context_.caller.submit(
            build_slug_request("link.preview", slug, m_ids_.next()),
            [this, epoch](const commands::CommandResponse& response)
            {
                if (epoch != m_preview_epoch_) {
                    return;  // superseded; a newer action took over
                }
                m_preview_token_ = 0;
                if (!response.ok) {
                    m_toasts_.push(toast_from_response(
                        "link.preview", response.id, response));
                    return;
                }
                open_result_view(response.result_json, "link.preview");
            });
    }

    /// Cancels any in-flight `link.preview` and bumps the epoch so a late
    /// completion is dropped (defence in depth over @ref AsyncCaller::cancel).
    void cancel_preview()
    {
        if (m_preview_token_ != 0) {
            m_context_.caller.cancel(m_preview_token_);
            m_preview_token_ = 0;
        }
        ++m_preview_epoch_;
    }

    /// Opens the inline Link detail / stats screen (D4, subtask 06.0/13) for
    /// the selected link. The slug is handed forward via
    /// @ref ScreenContext::pending_slug, which the detail screen consumes on
    /// enter; the detail is hosted inline (like the Jobs screen's job detail)
    /// because the D1 frame renders only tab screens and @ref App's global Esc
    /// pops the router before a nested component sees it. An in-flight preview
    /// is dropped so its viewer cannot open behind the detail.
    void open_detail_selected()
    {
        const std::string slug = selected_slug();
        if (slug.empty()) {
            return;
        }
        cancel_preview();
        m_context_.pending_slug = slug;
        m_detail_screen_ = make_link_detail_screen(
            m_context_, m_catalog_, [this] { m_detail_close_pending_ = true; });
        m_detail_component_ = m_detail_screen_->component();
        Add(m_detail_component_);
        m_mode_ = Mode::detail;
        m_detail_screen_->on_enter();
    }

    /// Tears down the inline detail screen and returns to the table (deferred
    /// from the detail's close callback to a safe point in @ref OnEvent /
    /// @ref OnRender).
    void finish_detail_close()
    {
        if (m_detail_screen_) {
            m_detail_screen_->on_leave();  // cancels its in-flight calls
        }
        if (m_detail_component_) {
            m_detail_component_->Detach();
            m_detail_component_.reset();
        }
        m_detail_screen_.reset();
        m_detail_close_pending_ = false;
        m_mode_ = Mode::table;
        m_context_.text_input_active = false;
    }

    // --- Inline form (new / edit) --------------------------------------------

    /// Opens the `link.create` form.
    void start_create()
    {
        const ActionAvailability avail =
            m_model_.availability(LinkAction::create, selected_status());
        if (!avail.enabled) {
            toast_unavailable(avail.reason, "link.create");
            return;
        }
        open_form("link.create", std::string(), "New link");
    }

    /// Opens the `link.update` form prefilled from the selected row.
    void start_edit()
    {
        const std::string status = selected_status();
        const ActionAvailability avail =
            m_model_.availability(LinkAction::edit, status);
        if (!avail.enabled) {
            toast_unavailable(avail.reason, "link.update");
            return;
        }
        const std::string* raw = m_table_.selected_raw();
        if (raw == nullptr) {
            toast_unavailable("select a link first", "link.update");
            return;
        }
        open_form("link.update", *raw, "Edit link");
        // link.update has no target_url argument, so the target cannot change.
        push_info(
            "target URL cannot be changed; create a new link to change "
            "it");
    }

    /// Builds the generated form for @p command, optionally prefilled, and
    /// shows it inline.
    void open_form(const std::string& command,
                   const std::string& prefill,
                   const std::string& title)
    {
        const CommandInfo* info = find_command(command);
        if (info == nullptr) {
            toast_unavailable("command unavailable", command);
            return;
        }
        m_form_model_ = std::make_unique<FormModel>(*info);
        if (!prefill.empty()) {
            m_form_model_->prefill_from_result(prefill);
        }
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

    /// After a successful create/update: close the form and refresh the table
    /// (the flow already raised the success toast).
    void on_form_success(const commands::CommandResponse& response)
    {
        if (!response.ok || response.job.has_value()) {
            return;  // keep the form open; the flow toasted the outcome
        }
        m_form_close_pending_ = true;
        refresh();
    }

    /// Detaches and drops the finished form, returning to the table.
    void finish_form_close()
    {
        if (m_form_) {
            m_form_->Detach();
            m_form_.reset();
        }
        m_form_ctx_.reset();
        m_form_model_.reset();
        m_form_close_pending_ = false;
        m_mode_ = Mode::table;
        m_context_.text_input_active = false;
    }

    // --- Inline result viewer (detail / preview) -----------------------------

    /// Opens the scrollable viewer over @p json titled @p title.
    void open_result_view(const std::string& json, const std::string& title)
    {
        m_result_ = std::make_unique<ResultViewerModel>(json, view_width());
        m_result_title_ = title;
        m_mode_ = Mode::result;
        // The viewer is modal; claim the keyboard so Esc/scroll reach the
        // screen rather than the global tab/back handler.
        m_context_.text_input_active = true;
    }

    /// Closes the viewer and returns to the table.
    void close_result()
    {
        cancel_preview();  // a late preview must not reopen a viewer
        m_result_.reset();
        m_mode_ = Mode::table;
        m_context_.text_input_active = false;
    }

    /// Viewer keys: scroll, Esc back (modal).
    bool result_keys(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Escape) {
            close_result();
            return true;
        }
        if (m_result_) {
            if (event == Event::Character("j") || event == Event::ArrowDown) {
                m_result_->scroll(1);
            }
            else if (event == Event::Character("k") || event == Event::ArrowUp)
            {
                m_result_->scroll(-1);
            }
            else if (event == Event::PageDown) {
                m_result_->page(1);
            }
            else if (event == Event::PageUp) {
                m_result_->page(-1);
            }
        }
        return true;  // the viewer is modal
    }

    // --- Rendering -----------------------------------------------------------

    /// The table body plus the in-body D3 action-key hint line.
    ftxui::Element render_table()
    {
        using namespace ftxui;
        return vbox({m_table_view_->Render() | flex, action_hints_line()});
    }

    /// The D3 status radio shown on the title line via `header_extra`. Built
    /// only from fixed labels (no server data), so the trusted-caller boundary
    /// is safe (review 6).
    ftxui::Element status_radio() const
    {
        using namespace ftxui;
        struct Item
        {
            StatusFilter status;
            const char* label;
        };
        static const Item items[] = {
            {StatusFilter::all, "all"},
            {StatusFilter::active, "active"},
            {StatusFilter::disabled, "disabled"},
            {StatusFilter::deleted, "deleted"},
        };
        Elements cells;
        cells.push_back(text("status: ") | to_decorator(m_theme_.hint));
        bool first = true;
        bool any_disabled = false;
        for (const Item& item : items) {
            if (!first) {
                cells.push_back(text(" "));
            }
            first = false;
            const bool selected = item.status == m_status_;
            const std::string label =
                selected ? ("(" + std::string(item.label) + ")") : item.label;
            Element cell = text(label);
            // Review A (display): a status the session cannot query
            // (all/deleted in read-only) renders disabled, not hidden (ADR 0003
            // rule 5).
            if (!status_queryable(item.status)) {
                cell = cell | to_decorator(m_theme_.disabled);
                any_disabled = true;
            }
            else if (selected) {
                cell = cell | to_decorator(m_theme_.selected_row);
            }
            cells.push_back(std::move(cell));
        }
        if (any_disabled) {
            cells.push_back(text("  (all/deleted: not_authorized, read-only)")
                            | to_decorator(m_theme_.disabled));
        }
        return hbox(std::move(cells));
    }

    /// The in-body D3 footer keys; the write keys dim when writes are disabled.
    ftxui::Element action_hints_line() const
    {
        using namespace ftxui;
        // The one capability decision (subtask 18): whether this session may
        // run any write at all, used to dim the write-key hints.
        CommandInfo write_probe;
        write_probe.permission = "write";
        const bool writes_disabled =
            !capability_for(m_context_).can_run(write_probe).allowed;
        const auto seg = [&](const char* label, bool write) -> Element
        {
            Element element = text(label);
            if (write && writes_disabled) {
                element = element | to_decorator(m_theme_.disabled);
            }
            return element;
        };
        return hbox({
            seg("Enter detail", false),
            text("  "),
            seg("n new", true),
            text("  "),
            seg("e edit", true),
            text("  "),
            seg("t toggle", true),
            text("  "),
            seg("d delete", true),
            text("  "),
            seg("u restore", true),
            text("  "),
            seg("p preview", false),
            text("  "),
            seg("s stats", false),
        });
    }

    /// The scrollable detail/preview viewer body.
    ftxui::Element render_result()
    {
        using namespace ftxui;
        const std::size_t width = view_width();
        Elements lines;
        for (const std::string& line : m_result_->visible_lines(view_height()))
        {
            lines.push_back(text(truncate_to_width(line, width)));
        }
        if (lines.empty()) {
            lines.push_back(text("(empty)") | to_decorator(m_theme_.hint));
        }
        std::string footer = "j/k scroll  Esc back";
        if (m_result_->truncated()) {
            footer += "   (truncated)";
        }
        Element content = vbox({
            vbox(std::move(lines)) | flex,
            separator(),
            text(footer) | to_decorator(m_theme_.hint),
        });
        const std::string title = " "
            + truncate_to_width(sanitize_for_terminal(m_result_title_), 40)
            + " ";
        return window(text(title) | to_decorator(m_theme_.title),
                      std::move(content));
    }

    /// The inner content width from the last frame, or the 80-column guess.
    int measured_width() const
    {
        const int width = m_box_.x_max - m_box_.x_min + 1;
        return width > 1 ? width : k_default_width;
    }

    /// Wrapping width handed to the result viewer (inside the window border).
    std::size_t view_width() const
    {
        const int width = measured_width() - 4;
        return static_cast<std::size_t>(width > 8 ? width : 72);
    }

    /// Rows of result text visible inside the viewer window.
    std::size_t view_height() const
    {
        const int height = m_box_.y_max - m_box_.y_min + 1;
        const int rows = height - 4;
        return static_cast<std::size_t>(rows > 1 ? rows : 18);
    }

    // --- Toasts --------------------------------------------------------------

    /// Raises the fixed "not available / not authorized" notice for a refused
    /// action.
    void toast_unavailable(const std::string& reason,
                           const std::string& command)
    {
        Toast toast;
        toast.kind = ToastKind::error;
        toast.title = "! not_available";
        toast.body = reason;
        toast.source_command = command;
        m_toasts_.push(std::move(toast));
    }

    /// Raises a neutral informational notice (e.g. the edit target-URL hint).
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

    ///< Command catalog used to resolve each mutation's @ref CommandInfo.
    std::vector<CommandInfo> m_catalog_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< Wall clock backing the toast queue and the staleness check.
    SystemClock m_clock_;

    ///< Toasts pushed by the actions and the submit flows.
    ToastQueue m_toasts_;

    ///< Confirm policy for the bespoke mutations (honors `--yes`).
    ConfirmPolicy m_policy_;

    ///< The confirm/submit choke point for the bespoke toggle/delete/restore
    ///< mutations (the inline form has its own internal flow).
    MutationFlow m_flow_;

    ///< The D3 model: columns, action availability, row mapping.
    LinksModel m_model_;

    ///< The paged table state machine over the D3 columns.
    TableModel m_table_;

    ///< Source of correlation ids for every request this screen makes.
    RequestIdGenerator m_ids_;

    ///< The reusable table view over @ref m_table_.
    ftxui::Component m_table_view_;

    ///< The current status radio choice; default `all` (decision log Q9).
    StatusFilter m_status_ = StatusFilter::all;

    ///< Token of the in-flight `link.list` call; `0` when none is pending.
    CallToken m_list_token_ = 0;

    ///< Token of the in-flight `link.preview` call; `0` when none is pending.
    CallToken m_preview_token_ = 0;

    ///< Monotonic tag for preview submissions; a completion whose captured
    ///< epoch no longer matches is dropped (stale-response guard).
    std::uint64_t m_preview_epoch_ = 0;

    ///< `true` once a page has loaded successfully (drives the stale check).
    bool m_loaded_ = false;

    ///< When the last successful page loaded; compared against @ref
    ///< k_stale_after.
    std::chrono::system_clock::time_point m_last_loaded_;

    ///< Current screen state.
    Mode m_mode_ = Mode::table;

    ///< The edited form model while @ref Mode::form; heap-held so the view
    ///< context's reference stays stable.
    std::unique_ptr<FormModel> m_form_model_;

    ///< The form view context; heap-held so its reference members stay stable.
    std::unique_ptr<FormViewContext> m_form_ctx_;

    ///< The generated form component while editing.
    ftxui::Component m_form_;

    ///< `true` when the form asked to close; the reset is deferred to a safe
    ///< point in @ref OnEvent / @ref OnRender.
    bool m_form_close_pending_ = false;

    ///< The inline Link detail screen while @ref Mode::detail (subtask 13),
    ///< held as @ref IScreen so its lifecycle hooks can be driven; null
    ///< otherwise.
    std::unique_ptr<IScreen> m_detail_screen_;

    ///< The detail screen's component, added as a child so the focus chain
    ///< reaches it; null when no detail is open.
    ftxui::Component m_detail_component_;

    ///< `true` when the detail asked to close (its `q`/`Esc`/auto-pop
    ///< callback); the teardown is deferred to a safe point in @ref OnEvent /
    ///< @ref OnRender.
    bool m_detail_close_pending_ = false;

    ///< The preview viewer while @ref Mode::result.
    std::unique_ptr<ResultViewerModel> m_result_;

    ///< The viewer window title (fixed text).
    std::string m_result_title_;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;
};

/**
 * @brief The Links tab screen (D3 node): wraps @ref LinksComponent as an
 * @ref IScreen.
 *
 * The D3 action keys are rendered in-body (the D3 sketch shows them inside the
 * table frame), so @ref hints returns empty to avoid duplicating them in the
 * global footer.
 */
class LinksScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context and @p catalog.
     * @param context The screen context; captured by the component by
     * reference.
     * @param catalog The command catalog; moved in.
     */
    LinksScreen(ScreenContext& context, std::vector<CommandInfo> catalog)
        : m_impl_(ftxui::Make<LinksComponent>(context, std::move(catalog)))
    {
    }

    ScreenId id() const override { return ScreenId::links; }

    ftxui::Component component() override { return m_impl_; }

    std::vector<KeyHint> hints() const override { return {}; }

    void on_enter() override { m_impl_->on_enter_refresh(); }

    void on_leave() override { m_impl_->on_leave_cleanup(); }

private:
    ///< The Links body component (kept as the derived type so @ref on_enter can
    ///< reach @ref LinksComponent::on_enter_refresh).
    std::shared_ptr<LinksComponent> m_impl_;
};

}  // namespace

std::unique_ptr<IScreen> make_links_screen(ScreenContext& context,
                                           std::vector<CommandInfo> commands)
{
    return std::make_unique<LinksScreen>(context, std::move(commands));
}

}  // namespace url_shortener::tui

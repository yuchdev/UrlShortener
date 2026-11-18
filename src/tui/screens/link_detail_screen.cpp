/**
 * @file link_detail_screen.cpp
 * @brief The Link detail / stats / preview screen (D4, subtask 06.0/13). It
 * fetches `link.get` and `link.stats` concurrently through @ref AsyncCaller,
 * renders the D4 two-column grid and the 7-day stats strip from
 * @ref LinkDetailModel (each pane independent, so a failing stats call never
 * blanks the link fields), and on demand shows the `link.preview` result in a
 * scrollable viewer and the generated `link.update` form inline.
 *
 * Design note (consistent with the Jobs screen, subtask 06.0/16): the D1 frame
 * renders only tab screens and @ref App's global Esc pops the router before any
 * nested component sees the event, so the Link detail (the D11 `link_detail`
 * node) is rendered *inline* within its owning Links tab rather than registered
 * as a router/tab screen, and `e` opens the edit form inline rather than
 * pushing the `link_form` node. Every mutating submit (edit / restore) routes
 * through
 * @ref run_mutation with the session read-only flag (ADR 0003 rule 4, threat
 * model T-116); the screen issues no command through @ref ICommandClient
 * directly (contract C8), tokenizes no JSON itself (it reads results through
 * the detail model / `json_view`, contract C9), and sanitizes every
 * server/stored string before display (threat model T-110). Async handlers
 * capture a `weak_ptr` alive-guard and an epoch, never a raw screen pointer, so
 * a stale/late response is dropped and never touches a destroyed screen
 * (T-120).
 */
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/screens/link_detail_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/ascii.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/form_model.hpp>
#include <url_shortener/tui/form_view.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/link_detail_model.hpp>
#include <url_shortener/tui/links_query.hpp>
#include <url_shortener/tui/mutation_flow.hpp>
#include <url_shortener/tui/result_viewer_model.hpp>
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

/// The dim label column width in the detail grid.
constexpr int k_label_width = 9;

/// Maximum glyphs in the compact stats sparkline (bounds display cost, T-121).
constexpr std::size_t k_sparkline_width = 60;

/// Sanitize (T-110) then truncate @p raw to at most @p width columns.
std::string fit(const std::string& raw, std::size_t width)
{
    return truncate_to_width(sanitize_for_terminal(raw), width);
}

/// Recovers a `link.*` descriptor from the catalog; a missing descriptor falls
/// back to a `write`/mutating command so the read-only + confirm gate stays
/// fail-closed (threat model T-116).
CommandInfo find_info(const std::vector<CommandInfo>& catalog,
                      const std::string& name)
{
    for (const CommandInfo& info : catalog) {
        if (info.name == name) {
            return info;
        }
    }
    CommandInfo fallback;
    fallback.name = name;
    fallback.permission = "write";  // fail closed
    fallback.mutating = true;
    return fallback;
}

/// Reports whether the `link.stats` descriptor accepts a `bucket` argument, so
/// the `s` bucket toggle is offered only when the server supports it (spec step
/// 4). A missing descriptor (fail-closed fallback has no args) reports `false`.
bool stats_has_bucket_arg(const std::vector<CommandInfo>& catalog)
{
    for (const CommandInfo& info : catalog) {
        if (info.name != "link.stats") {
            continue;
        }
        for (const ArgInfo& arg : info.args) {
            if (arg.name == "bucket") {
                return true;
            }
        }
        return false;
    }
    return false;
}

/**
 * @brief The Link detail body: the D4 grid + stats strip, with an inline
 * `link.preview` viewer and `link.update` edit form (subtask 06.0/13).
 */
class LinkDetailComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the component over @p context and @p catalog.
     *
     * @param context The screen context; its caller/router/options are owned by
     * @ref App and captured by reference, so they must outlive this component.
     * @param catalog The command catalog, read to resolve the `link.update` /
     * `link.restore` descriptors; moved in.
     * @param on_close Invoked when the operator leaves the detail (`q`/`Esc`)
     * or a `not_found` auto-pop fires; must only set a deferred flag, never
     * destroy this component synchronously. Moved in; may be empty.
     */
    LinkDetailComponent(ScreenContext& context,
                        std::vector<CommandInfo> catalog,
                        std::function<void()> on_close)
        : m_alive_(std::make_shared<bool>(true))
        , m_context_(context)
        , m_catalog_(std::move(catalog))
        , m_theme_(theme_for(context.options.theme))
        , m_on_close_(std::move(on_close))
        , m_toasts_(m_clock_)
        , m_policy_(context.options.assume_yes)
        , m_model_(m_clock_)
        , m_flow_(MutationDeps {context.caller,
                                m_policy_,
                                m_toasts_,
                                m_theme_,
                                context.options.read_only})
    {
        // Spec step 4: the `s` bucket toggle is offered only when the live
        // `link.stats` descriptor actually accepts a `bucket` argument; read it
        // from the catalog at runtime so the key (and its hint) vanish when the
        // server does not support bucketed stats.
        m_stats_bucket_supported_ = stats_has_bucket_arg(m_catalog_);
    }

    /// Called when the detail becomes active: read the handed-over slug and
    /// start the two concurrent fetches (implementation step 1).
    void on_enter()
    {
        m_slug_ = m_context_.pending_slug;
        m_mode_ = Mode::detail;
        fetch_link();
        fetch_stats();
    }

    /// Called when the detail is left: drop any in-flight calls so a late
    /// response cannot open a viewer or mutate a dead screen.
    void on_leave() { cancel_all(); }

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
        else if (m_mode_ == Mode::preview && m_preview_) {
            body = render_preview();
        }
        else {
            body = render_detail();
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
        using namespace ftxui;
        if (m_form_close_pending_) {
            finish_form_close();
        }
        if (m_mode_ == Mode::form && m_form_) {
            const bool consumed = m_form_->OnEvent(event);
            if (m_form_close_pending_) {
                finish_form_close();
            }
            return consumed;
        }
        if (m_mode_ == Mode::preview) {
            return preview_keys(event);
        }
        // Detail mode. A restore confirm dialog is modal over the grid.
        if (m_flow_.awaiting_confirm()) {
            const bool handled = m_flow_.overlay_event(event);
            m_context_.text_input_active = m_flow_.awaiting_confirm();
            return handled;
        }
        return detail_keys(event);
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

    /// The open form drives FTXUI's focus chain while editing; otherwise the
    /// detail owns input directly (it has no interactive child).
    ftxui::Component ActiveChild() override
    {
        if (m_mode_ == Mode::form && m_form_) {
            return m_form_;
        }
        return nullptr;
    }

private:
    ///< The three screen states: the detail grid, the edit form, the preview.
    enum class Mode
    {
        detail,
        form,
        preview
    };

    // --- Data loading --------------------------------------------------------

    /// Re-fetches both panes (the `r` refresh and the post-edit refresh).
    void refresh()
    {
        fetch_link();
        fetch_stats();
    }

    /// Submits `link.get` for the current slug, dropping any in-flight one.
    void fetch_link()
    {
        if (m_link_token_ != 0) {
            m_context_.caller.cancel(m_link_token_);
        }
        const std::uint64_t epoch = ++m_link_epoch_;
        std::weak_ptr<bool> guard = m_alive_;
        m_link_token_ = m_context_.caller.submit(
            m_model_.get_request(m_slug_, m_ids_.next()),
            [this, guard, epoch](const commands::CommandResponse& response)
            {
                if (guard.expired() || epoch != m_link_epoch_) {
                    return;  // the screen is gone, or a newer fetch superseded
                }
                m_link_token_ = 0;
                on_link_response(response);
            });
    }

    /// Submits `link.stats` for the current slug, dropping any in-flight one.
    void fetch_stats()
    {
        if (m_stats_token_ != 0) {
            m_context_.caller.cancel(m_stats_token_);
        }
        const std::uint64_t epoch = ++m_stats_epoch_;
        commands::CommandRequest request = m_model_.stats_request(m_slug_);
        request.id = m_ids_.next();
        std::weak_ptr<bool> guard = m_alive_;
        m_stats_token_ = m_context_.caller.submit(
            std::move(request),
            [this, guard, epoch](const commands::CommandResponse& response)
            {
                if (guard.expired() || epoch != m_stats_epoch_) {
                    return;
                }
                m_stats_token_ = 0;
                on_stats_response(response);
            });
    }

    /// Applies a `link.get` response: a `not_found` toasts and auto-pops; any
    /// other failure records the error (the stats pane is untouched).
    void on_link_response(const commands::CommandResponse& response)
    {
        if (!response.ok) {
            const std::string code =
                std::string(commands::error_info_for(response.error.code).name);
            m_model_.apply_link_error(code, response.error.message);
            m_toasts_.push(
                toast_from_response("link.get", response.id, response));
            if (response.error.code == commands::CommandErrorCode::not_found
                && m_on_close_)
            {
                m_on_close_();  // the link is gone; return to the list
            }
            return;
        }
        m_model_.apply_link(response.result_json);
    }

    /// Applies a `link.stats` response: success renders the strip, a failure
    /// shows `stats unavailable: <code>` and leaves the link pane intact.
    void on_stats_response(const commands::CommandResponse& response)
    {
        if (!response.ok) {
            const std::string code =
                std::string(commands::error_info_for(response.error.code).name);
            m_model_.apply_stats_error(code, response.error.message);
            return;
        }
        m_model_.apply_stats(response.result_json);
    }

    /// Cancels every in-flight call and bumps each epoch so a late completion
    /// is dropped (defence in depth over @ref AsyncCaller::cancel).
    void cancel_all()
    {
        if (m_link_token_ != 0) {
            m_context_.caller.cancel(m_link_token_);
            m_link_token_ = 0;
        }
        if (m_stats_token_ != 0) {
            m_context_.caller.cancel(m_stats_token_);
            m_stats_token_ = 0;
        }
        cancel_preview();
        m_flow_.cancel();  // suppress an in-flight restore before teardown
        ++m_link_epoch_;
        ++m_stats_epoch_;
    }

    // --- Actions -------------------------------------------------------------

    /// Routes the detail-mode keys; returns `true` when one is consumed.
    bool detail_keys(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Character("q") || event == Event::Escape) {
            if (m_on_close_) {
                m_on_close_();
            }
            return true;
        }
        if (event == Event::Character("e")) {
            start_edit();
            return true;
        }
        if (event == Event::Character("p")) {
            do_preview();
            return true;
        }
        if (event == Event::Character("s")) {
            if (!m_stats_bucket_supported_) {
                return false;  // bucket toggle hidden; leave the key unhandled
            }
            toggle_stats();
            return true;
        }
        if (event == Event::Character("r")) {
            refresh();
            return true;
        }
        if (event == Event::Character("u")) {
            do_restore();
            return true;
        }
        return false;
    }

    /// Toggles the stats bucket between `day` and `hour` and refetches stats.
    void toggle_stats()
    {
        m_model_.set_bucket(m_model_.bucket() == "day" ? "hour" : "day");
        fetch_stats();
    }

    /// Submits `link.preview` and shows the result in the inline viewer, with
    /// the same stale-response discipline as the fetches (a late response
    /// cannot open the viewer after the operator has moved on).
    void do_preview()
    {
        if (m_slug_.empty()) {
            return;
        }
        cancel_preview();
        const std::uint64_t epoch = m_preview_epoch_;
        std::weak_ptr<bool> guard = m_alive_;
        m_preview_token_ = m_context_.caller.submit(
            build_slug_request("link.preview", m_slug_, m_ids_.next()),
            [this, guard, epoch](const commands::CommandResponse& response)
            {
                if (guard.expired() || epoch != m_preview_epoch_) {
                    return;
                }
                m_preview_token_ = 0;
                if (!response.ok) {
                    m_toasts_.push(toast_from_response(
                        "link.preview", response.id, response));
                    return;
                }
                open_preview(response.result_json);
            });
    }

    /// Cancels any in-flight `link.preview` and bumps its epoch so a late
    /// completion is dropped.
    void cancel_preview()
    {
        if (m_preview_token_ != 0) {
            m_context_.caller.cancel(m_preview_token_);
            m_preview_token_ = 0;
        }
        ++m_preview_epoch_;
    }

    /// Restores a deleted link through the confirm/read-only choke point; a
    /// non-deleted link is a no-op notice (implementation step 5).
    void do_restore()
    {
        if (!m_model_.is_deleted()) {
            Toast toast;
            toast.kind = ToastKind::info;
            toast.title = "link.restore";
            toast.body = "only a deleted link can be restored";
            toast.source_command = "link.restore";
            m_toasts_.push(std::move(toast));
            return;
        }
        const CommandInfo info = find_info(m_catalog_, "link.restore");
        run_mutation(m_flow_,
                     build_slug_request("link.restore", m_slug_, m_ids_.next()),
                     info,
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

    // --- Inline edit form (link.update) --------------------------------------

    /// Opens the generated `link.update` form prefilled from the fetched JSON.
    void start_edit()
    {
        if (!m_model_.has_link()) {
            return;  // nothing fetched yet
        }
        const CommandInfo info = find_info(m_catalog_, "link.update");
        m_form_model_ = std::make_unique<FormModel>(info);
        m_form_model_->prefill_from_result(m_model_.link_json());
        FormViewContext view_ctx {
            m_context_.caller,
            m_policy_,
            m_toasts_,
            m_theme_,
            m_context_.options.read_only,
            "Edit link",
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

    /// After a successful update: close the form and refresh both panes.
    void on_form_success(const commands::CommandResponse& response)
    {
        if (!response.ok || response.job.has_value()) {
            return;  // keep the form open; the flow toasted the outcome
        }
        m_form_close_pending_ = true;
        refresh();
    }

    /// Detaches and drops the finished form, returning to the detail grid.
    void finish_form_close()
    {
        if (m_form_) {
            m_form_->Detach();
            m_form_.reset();
        }
        m_form_ctx_.reset();
        m_form_model_.reset();
        m_form_close_pending_ = false;
        m_mode_ = Mode::detail;
        m_context_.text_input_active = false;
    }

    // --- Inline preview viewer -----------------------------------------------

    /// Opens the scrollable preview viewer over @p json.
    void open_preview(const std::string& json)
    {
        m_preview_ = std::make_unique<ResultViewerModel>(json, view_width());
        m_mode_ = Mode::preview;
        // The viewer is modal; claim the keyboard so Esc/scroll reach it.
        m_context_.text_input_active = true;
    }

    /// Closes the preview viewer and returns to the detail grid.
    void close_preview()
    {
        cancel_preview();
        m_preview_.reset();
        m_mode_ = Mode::detail;
        m_context_.text_input_active = false;
    }

    /// Preview-viewer keys: scroll, Esc back (modal).
    bool preview_keys(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Escape) {
            close_preview();
            return true;
        }
        if (m_preview_) {
            if (event == Event::Character("j") || event == Event::ArrowDown) {
                m_preview_->scroll(1);
            }
            else if (event == Event::Character("k") || event == Event::ArrowUp)
            {
                m_preview_->scroll(-1);
            }
            else if (event == Event::PageDown) {
                m_preview_->page(1);
            }
            else if (event == Event::PageUp) {
                m_preview_->page(-1);
            }
        }
        return true;  // the preview is modal
    }

    // --- Rendering -----------------------------------------------------------

    /// The detail grid + stats strip inside the D4 window.
    ftxui::Element render_detail()
    {
        using namespace ftxui;
        const std::size_t width = content_width();
        Elements lines;
        if (m_model_.has_link()) {
            for (const DetailRow& row : m_model_.rows()) {
                lines.push_back(render_row(row, width));
            }
        }
        else if (m_model_.link_failed()) {
            lines.push_back(
                text(fit("link unavailable: " + m_model_.link_error_code(),
                         width))
                | to_decorator(m_theme_.error));
        }
        else {
            lines.push_back(text("loading...") | to_decorator(m_theme_.hint));
        }

        lines.push_back(separator());
        append_stats(lines, width);

        const std::string stats_hint =
            m_stats_bucket_supported_ ? "s stats  " : "";
        Element content = vbox({
            vbox(std::move(lines)) | flex,
            separator(),
            text(fit("e edit  p preview  " + stats_hint
                         + "r refresh  u restore  q back",
                     width))
                | to_decorator(m_theme_.hint),
        });
        const std::string title_hint =
            m_stats_bucket_supported_ ? "   [e]dit [s]tats " : "   [e]dit ";
        const std::string title = " link " + fit(m_slug_, 40) + title_hint;
        return window(text(title) | to_decorator(m_theme_.title),
                      std::move(content));
    }

    /// Renders one D4 grid row (two columns, or a full-width value).
    ftxui::Element render_row(const DetailRow& row, std::size_t width)
    {
        using namespace ftxui;
        const auto label = [this](const std::string& text_value)
        {
            return text(pad_to_width(text_value, k_label_width))
                | to_decorator(m_theme_.hint);
        };
        if (row.right_label.empty()) {
            const std::size_t value_width =
                width > static_cast<std::size_t>(k_label_width + 1)
                ? width - static_cast<std::size_t>(k_label_width + 1)
                : 8u;
            return hbox({label(row.left_label),
                         text(" "),
                         text(fit(row.left_value, value_width))});
        }
        const std::size_t columns =
            width > static_cast<std::size_t>(2 * (k_label_width + 1) + 2)
            ? (width - static_cast<std::size_t>(2 * (k_label_width + 1) + 2))
                / 2
            : 8u;
        return hbox({label(row.left_label),
                     text(" "),
                     text(fit(row.left_value, columns)),
                     text("  "),
                     label(row.right_label),
                     text(" "),
                     text(fit(row.right_value, columns))});
    }

    /// Appends the stats header, compact sparkline and per-day counts (or the
    /// `stats unavailable` line) to @p lines.
    void append_stats(ftxui::Elements& lines, std::size_t width)
    {
        using namespace ftxui;
        lines.push_back(text(fit(m_model_.stats_header(), width)));
        if (m_model_.stats_failed()) {
            lines.push_back(
                text(fit("stats unavailable: " + m_model_.stats_error_code(),
                         width))
                | to_decorator(m_theme_.error));
            return;
        }
        if (!m_model_.has_stats()) {
            lines.push_back(text("loading stats...")
                            | to_decorator(m_theme_.hint));
            return;
        }
        const std::vector<std::pair<std::string, std::uint64_t>> bars =
            m_model_.day_bars();
        std::vector<std::uint64_t> counts;
        counts.reserve(bars.size());
        std::string counts_line;
        bool first = true;
        for (const std::pair<std::string, std::uint64_t>& bar : bars) {
            counts.push_back(bar.second);
            if (!first) {
                counts_line += "  ";
            }
            first = false;
            counts_line += bar.first;  // the weekday label (ASCII, trusted)
            counts_line += ' ';
            counts_line += format_count(bar.second);
        }
        const std::string spark =
            sparkline(counts, k_sparkline_width, m_theme_.ascii_only);
        lines.push_back(text(fit("trend " + spark, width)));
        lines.push_back(text(fit(counts_line, width)));
    }

    /// The scrollable preview viewer body.
    ftxui::Element render_preview()
    {
        using namespace ftxui;
        const std::size_t width = view_width();
        Elements lines;
        for (const std::string& line : m_preview_->visible_lines(view_height()))
        {
            lines.push_back(text(truncate_to_width(line, width)));
        }
        if (lines.empty()) {
            lines.push_back(text("(empty)") | to_decorator(m_theme_.hint));
        }
        std::string footer = "j/k scroll  Esc back";
        if (m_preview_->truncated()) {
            footer += "   (truncated)";
        }
        Element content = vbox({
            vbox(std::move(lines)) | flex,
            separator(),
            text(footer) | to_decorator(m_theme_.hint),
        });
        const std::string title = " preview " + fit(m_slug_, 40) + " ";
        return window(text(title) | to_decorator(m_theme_.title),
                      std::move(content));
    }

    /// The inner content width from the last frame, or the 80-column guess.
    int measured_width() const
    {
        const int width = m_box_.x_max - m_box_.x_min + 1;
        return width > 1 ? width : k_default_width;
    }

    /// Content width inside the window border.
    std::size_t content_width() const
    {
        const int width = measured_width() - 4;
        return static_cast<std::size_t>(width > 8 ? width : 72);
    }

    /// Wrapping width handed to the preview viewer (inside the window border).
    std::size_t view_width() const { return content_width(); }

    /// Rows of preview text visible inside the viewer window.
    std::size_t view_height() const
    {
        const int height = m_box_.y_max - m_box_.y_min + 1;
        const int rows = height - 4;
        return static_cast<std::size_t>(rows > 1 ? rows : 18);
    }

    ///< Alive guard shared with every async handler; cleared by this object's
    ///< destruction so a late task is a no-op (T-120). Declared first so it
    ///< outlives every other member.
    std::shared_ptr<bool> m_alive_;

    ///< Screen context (caller, session, router, options); not owned.
    ScreenContext& m_context_;

    ///< Command catalog used to resolve `link.update` / `link.restore`.
    std::vector<CommandInfo> m_catalog_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< Invoked on `q`/`Esc` or a `not_found` auto-pop; may be empty.
    std::function<void()> m_on_close_;

    ///< Wall clock backing the toast queue and the stats window.
    SystemClock m_clock_;

    ///< Toasts (fetch errors, restore outcome, no-op notices).
    ToastQueue m_toasts_;

    ///< Confirm policy for the restore mutation (honors `--yes`).
    ConfirmPolicy m_policy_;

    ///< The D4 detail/stats model (parses `link.get` / `link.stats`).
    LinkDetailModel m_model_;

    ///< The confirm/submit choke point for `link.restore` (the edit form has
    ///< its own internal flow).
    MutationFlow m_flow_;

    ///< Source of correlation ids for the screen's requests.
    RequestIdGenerator m_ids_;

    ///< The slug handed over by Links (consumed on @ref on_enter).
    std::string m_slug_;

    ///< `true` when the `link.stats` descriptor accepts a `bucket` arg, so the
    ///< `s` bucket toggle and its hint are shown (spec step 4).
    bool m_stats_bucket_supported_ = false;

    ///< Current screen state.
    Mode m_mode_ = Mode::detail;

    ///< Token of the in-flight `link.get`; `0` when none is pending.
    CallToken m_link_token_ = 0;

    ///< Token of the in-flight `link.stats`; `0` when none is pending.
    CallToken m_stats_token_ = 0;

    ///< Token of the in-flight `link.preview`; `0` when none is pending.
    CallToken m_preview_token_ = 0;

    ///< Monotonic tags for the three call kinds; a completion whose captured
    ///< epoch no longer matches is dropped (stale-response guard).
    std::uint64_t m_link_epoch_ = 0;
    std::uint64_t m_stats_epoch_ = 0;
    std::uint64_t m_preview_epoch_ = 0;

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

    ///< The preview viewer while @ref Mode::preview.
    std::unique_ptr<ResultViewerModel> m_preview_;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;
};

/**
 * @brief The Link detail screen (D4 node): wraps @ref LinkDetailComponent as an
 * @ref IScreen and forwards the active-screen lifecycle to it.
 */
class LinkDetailScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context, @p catalog and @p on_close.
     * @param context The screen context; captured by the component by
     * reference.
     * @param catalog The command catalog; moved in.
     * @param on_close The leave/auto-pop hook; moved in.
     */
    LinkDetailScreen(ScreenContext& context,
                     std::vector<CommandInfo> catalog,
                     std::function<void()> on_close)
        : m_body_(ftxui::Make<LinkDetailComponent>(
              context, std::move(catalog), std::move(on_close)))
    {
    }

    ScreenId id() const override { return ScreenId::link_detail; }

    ftxui::Component component() override { return m_body_; }

    std::vector<KeyHint> hints() const override
    {
        return {KeyHint {"e", "edit"},
                KeyHint {"p", "preview"},
                KeyHint {"s", "stats"},
                KeyHint {"r", "refresh"},
                KeyHint {"q/Esc", "back"}};
    }

    void on_enter() override { component_impl().on_enter(); }

    void on_leave() override { component_impl().on_leave(); }

private:
    /// Narrows the stored component to its concrete type for the lifecycle
    /// hops.
    LinkDetailComponent& component_impl()
    {
        return *static_cast<LinkDetailComponent*>(m_body_.get());
    }

    ///< The Link detail body component.
    ftxui::Component m_body_;
};

}  // namespace

std::unique_ptr<IScreen> make_link_detail_screen(
    ScreenContext& context,
    std::vector<CommandInfo> commands,
    std::function<void()> on_close)
{
    return std::make_unique<LinkDetailScreen>(
        context, std::move(commands), std::move(on_close));
}

}  // namespace url_shortener::tui

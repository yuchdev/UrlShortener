/**
 * @file jobs_screen.cpp
 * @brief The Jobs screen (D8, subtask 06.0/16): a polled `job.list` table with
 * cancel (`job.cancel`) and a full-envelope detail view (`job.get`, D11 edge
 * `Jobs -> JobDetail`). Rendering goes through the reusable `TableView` (every
 * cell sanitized, T-110) and a `ResultViewerModel` for the detail envelope; the
 * one mutating action routes through @ref url_shortener::tui::run_mutation with
 * the session read-only flag (ADR 0003 rule 4, threat model T-116). Every
 * command is submitted through the injected @ref
 * url_shortener::tui::AsyncCaller; a @ref url_shortener::tui::PollTicker only
 * posts a UI-thread tick and never reaches the command client (contracts C1/C8,
 * ADR 0003 rule 3). Handlers capture a `weak_ptr` alive-guard, never a raw
 * screen pointer (T-120).
 */
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/screens/jobs_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/jobs_model.hpp>
#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/mutation_flow.hpp>
#include <url_shortener/tui/poll_ticker.hpp>
#include <url_shortener/tui/result_viewer_model.hpp>
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

/// Sanitize (T-110) then truncate @p raw to at most @p width columns.
std::string fit(const std::string& raw, std::size_t width)
{
    return truncate_to_width(sanitize_for_terminal(raw), width);
}

/// Recovers the `job.cancel` descriptor from the catalog, forcing @ref
/// CommandInfo::mutating so a yes/no confirm is shown (the catalog marks
/// `job.cancel` `write` but not mutating-confirmed; a state change still earns
/// a confirm when not `--yes`, implementation step 3). The base permission is
/// kept so the read-only gate stays fail-closed; a missing descriptor falls
/// back to a `write` command.
CommandInfo find_cancel_info(const std::vector<CommandInfo>& catalog)
{
    for (const CommandInfo& info : catalog) {
        if (info.name == "job.cancel") {
            CommandInfo forced = info;
            forced.mutating = true;
            return forced;
        }
    }
    CommandInfo fallback;
    fallback.name = "job.cancel";
    fallback.permission = "write";  // fail closed
    fallback.mutating = true;
    return fallback;
}

/**
 * @brief The Jobs body: a polled `job.list` table plus an inline job detail
 * view (subtask 06.0/16).
 */
class JobsComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the component over @p context and @p commands.
     * @param context The screen context; its caller/router/options are owned by
     * @ref App and captured by reference, so they must outlive this component.
     * @param commands The catalog, read only to recover the `job.cancel`
     * descriptor; moved in.
     */
    JobsComponent(ScreenContext& context, std::vector<CommandInfo> commands)
        : m_alive_(std::make_shared<bool>(true))
        , m_context_(context)
        , m_toasts_(m_clock_)
        , m_policy_(context.options.assume_yes)
        , m_theme_(theme_for(context.options.theme))
        , m_cancel_info_(find_cancel_info(commands))
        , m_model_(m_theme_.ascii_only)
        , m_flow_(MutationDeps {context.caller,
                                m_policy_,
                                m_toasts_,
                                m_theme_,
                                capability_for(context)})
    {
        TableViewOptions opts;
        opts.title = "Jobs";
        opts.theme = &m_theme_;
        opts.show_filter_bar = false;  // Jobs is a polled list, not filtered
        opts.sort_key_enabled = false;  // no client sort; the server orders
        opts.on_activate = [this](std::size_t) { open_detail(); };
        opts.on_extra_key = [this](const ftxui::Event& ev,
                                   std::optional<std::size_t>) -> bool
        {
            if (ev == ftxui::Event::Character("c")) {
                begin_cancel();
                return true;
            }
            if (ev == ftxui::Event::Character("r")) {
                poll_list();
                return true;
            }
            return false;
        };
        opts.extra_hints = {KeyHint {"Enter", "detail"},
                            KeyHint {"c", "cancel"},
                            KeyHint {"r", "refresh"}};
        m_table_view_ = make_table_view(m_model_.table(), std::move(opts));
        Add(m_table_view_);

        m_ticker_ = std::make_unique<PollTicker>(
            m_context_.caller.poster(),
            [this, guard = std::weak_ptr<bool>(m_alive_)]
            {
                if (guard.expired()) {
                    return;  // the screen is gone; never touch it (T-120)
                }
                on_tick();
            });
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        Element body =
            m_mode_ == Mode::detail ? render_detail() : render_list();
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
        if (m_mode_ == Mode::detail) {
            return detail_keys(event);
        }
        if (m_flow_.awaiting_confirm()) {
            return m_flow_.overlay_event(event);  // the confirm is modal
        }
        return m_table_view_->OnEvent(event);
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

    /// The table view drives FTXUI's focus chain while listing.
    ftxui::Component ActiveChild() override { return m_table_view_; }

    /// Called when the Jobs tab becomes active: start polling (and land on a
    /// handed-over job id if Import/Export set one).
    void on_enter()
    {
        m_visible_ = true;
        if (!m_context_.focus_job_id.empty()) {
            m_pending_focus_ = m_context_.focus_job_id;
            m_context_.focus_job_id.clear();
        }
        poll_list();
        m_ticker_->set_interval(m_schedule_.current_delay());
        m_ticker_->start();
    }

    /// Called when the Jobs tab stops being active: stop polling once no job is
    /// still running, so the app does not spin on another tab (success
    /// criterion 3; @ref PollSchedule::should_poll truth table).
    void on_leave()
    {
        m_visible_ = false;
        if (!m_model_.any_active()) {
            m_ticker_->stop();
        }
    }

private:
    ///< The two screen states: the job list vs one job's detail envelope.
    enum class Mode
    {
        list,
        detail
    };

    /// Submits one `job.list {limit:50}`, unless a previous one is still in
    /// flight (skip, never queue - at most one in flight, success criterion 2).
    void poll_list()
    {
        ensure_ticking();  // a refresh re-arms the auto-poll if it went idle
        if (m_list_in_flight_) {
            return;
        }
        m_list_in_flight_ = true;
        commands::CommandRequest req;
        req.v = 1;
        req.id = m_ids_.next();
        req.command = "job.list";
        req.args_json = "{\"limit\":50}";
        std::weak_ptr<bool> guard = m_alive_;
        m_context_.caller.submit(
            std::move(req),
            [this, guard](const commands::CommandResponse& resp)
            {
                if (guard.expired()) {
                    return;  // the screen is gone (T-120)
                }
                m_list_in_flight_ = false;
                on_list_response(resp);
            });
    }

    /// Applies a `job.list` response: refresh the table on success, record the
    /// error code and back off on failure, then re-arm the ticker cadence.
    void on_list_response(const commands::CommandResponse& resp)
    {
        if (resp.ok) {
            m_last_error_.clear();
            m_model_.apply_list(resp.result_json);
            if (!m_pending_focus_.empty()) {
                m_model_.focus_job(m_pending_focus_);
                m_pending_focus_.clear();
            }
        }
        else {
            m_last_error_ =
                std::string(commands::error_info_for(resp.error.code).name);
        }
        m_schedule_.next_delay(resp.ok);
        m_ticker_->set_interval(m_schedule_.current_delay());
    }

    /// Begins a `job.cancel` for the selected job, or toasts a no-op when the
    /// job is already terminal (implementation step 3). Routes through the
    /// confirm/read-only choke point (ADR 0003 rule 4, T-116).
    void begin_cancel()
    {
        if (m_flow_.awaiting_confirm() || m_flow_.busy()) {
            return;
        }
        // A no-op/confirm/read-only toast or an async submit follows; keep the
        // ticker alive so a pushed toast still expires and a confirm can poll.
        ensure_ticking();
        if (!m_model_.can_cancel_selected()) {
            const std::string state = m_model_.selected_state();
            Toast t;
            t.kind = ToastKind::info;
            t.title = "job.cancel";
            t.body = state.empty() ? "no cancellable job selected"
                                   : "job already " + state;
            t.source_command = "job.cancel";
            m_toasts_.push(std::move(t));
            return;
        }
        const std::optional<std::string> id = m_model_.selected_id();
        if (!id.has_value()) {
            return;
        }
        commands::CommandRequest req;
        req.v = 1;
        req.id = m_ids_.next();
        req.command = "job.cancel";
        req.args_json = "{\"id\":" + url_shortener::jsonString(*id) + "}";
        run_mutation(m_flow_,
                     std::move(req),
                     m_cancel_info_,
                     [this](const commands::CommandResponse& resp)
                     {
                         if (resp.ok) {
                             poll_list();  // reflect the new state immediately
                         }
                     });
    }

    /// Opens the detail view for the selected job and fetches its envelope.
    void open_detail()
    {
        const std::optional<std::string> id = m_model_.selected_id();
        if (!id.has_value()) {
            return;
        }
        m_detail_id_ = *id;
        m_mode_ = Mode::detail;
        m_detail_view_.reset();
        m_detail_terminal_ = false;
        poll_detail();
    }

    /// Returns from the detail view to the list and refreshes it.
    void close_detail()
    {
        m_mode_ = Mode::list;
        m_detail_view_.reset();
        m_detail_id_.clear();
        m_detail_terminal_ = false;
        poll_list();
    }

    /// Submits one `job.get {id}` for the open detail, unless one is already in
    /// flight.
    void poll_detail()
    {
        ensure_ticking();  // re-arm auto-refresh if the ticker went idle
        if (m_detail_in_flight_ || m_detail_id_.empty()) {
            return;
        }
        m_detail_in_flight_ = true;
        commands::CommandRequest req;
        req.v = 1;
        req.id = m_ids_.next();
        req.command = "job.get";
        req.args_json =
            "{\"id\":" + url_shortener::jsonString(m_detail_id_) + "}";
        std::weak_ptr<bool> guard = m_alive_;
        m_context_.caller.submit(
            std::move(req),
            [this, guard](const commands::CommandResponse& resp)
            {
                if (guard.expired()) {
                    return;  // the screen is gone (T-120)
                }
                m_detail_in_flight_ = false;
                on_detail_response(resp);
            });
    }

    /// Shows the full response envelope and stops auto-refresh once the job is
    /// terminal (or the fetch itself failed).
    void on_detail_response(const commands::CommandResponse& resp)
    {
        // The detail shows the complete envelope (ok, result or error) - a
        // failed job's full error envelope (success criterion 4). The viewer
        // pretty-prints and sanitizes it (T-110).
        const std::string envelope = commands::serialize_response(resp);
        m_detail_view_ =
            std::make_unique<ResultViewerModel>(envelope, detail_width());
        const std::string state = resp.ok
            ? get_string(resp.result_json, "state").value_or("")
            : std::string();
        m_detail_terminal_ = !resp.ok || is_terminal_state(state);
        m_schedule_.next_delay(resp.ok);
        m_ticker_->set_interval(m_schedule_.current_delay());
    }

    /// The periodic tick (UI thread): poll the detail while open and
    /// non-terminal, else poll the list while it should keep polling; otherwise
    /// stop the ticker so the app goes idle (review 06.0/16 item 2).
    void on_tick()
    {
        m_toasts_.tick();
        bool did_poll = false;
        if (m_mode_ == Mode::detail) {
            if (!m_detail_terminal_) {
                poll_detail();
                did_poll = true;
            }
        }
        else if (m_schedule_.should_poll(m_visible_, m_model_.any_active())) {
            poll_list();
            did_poll = true;
        }
        // Stop the thread once there is nothing left to do - no ongoing poll
        // need (hidden list with no active jobs, or a terminal detail) and no
        // auto-expiring toast still pending - so the app does not wake at 1 Hz
        // doing nothing. Any action that needs polling again (leaving the
        // detail, a manual refresh, a new active job, a pushed toast) restarts
        // the ticker through @ref ensure_ticking.
        if (!did_poll && !toasts_pending()) {
            m_ticker_->stop();
        }
    }

    /// Whether an auto-expiring (info/success) toast is still visible and so
    /// needs the ticker to keep running to expire it. An error toast persists
    /// until dismissed and deliberately does not keep the ticker alive.
    bool toasts_pending() const
    {
        for (const Toast& t : m_toasts_.visible()) {
            if (t.kind != ToastKind::error) {
                return true;
            }
        }
        return false;
    }

    /// Restarts the poll ticker if it stopped itself while idle; a no-op when
    /// it is already running (@ref PollTicker::start is idempotent).
    void ensure_ticking() { m_ticker_->start(); }

    /// Detail-view keys: scroll, `r` refresh, `Esc` back.
    bool detail_keys(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Escape) {
            close_detail();
            return true;
        }
        if (event == Event::Character("r")) {
            poll_detail();
            return true;
        }
        if (m_detail_view_) {
            if (event == Event::Character("j") || event == Event::ArrowDown) {
                m_detail_view_->scroll(1);
            }
            else if (event == Event::Character("k") || event == Event::ArrowUp)
            {
                m_detail_view_->scroll(-1);
            }
            else if (event == Event::PageDown) {
                m_detail_view_->page(1);
            }
            else if (event == Event::PageUp) {
                m_detail_view_->page(-1);
            }
        }
        return true;  // the detail view is modal
    }

    /// The inner content width from the last frame, or the 80-column guess.
    int measured_width() const
    {
        const int w = m_box_.x_max - m_box_.x_min + 1;
        return w > 1 ? w : k_default_width;
    }

    /// Wrapping width handed to the detail result viewer (inside the border).
    std::size_t detail_width() const
    {
        const int w = measured_width() - 4;
        return static_cast<std::size_t>(w > 8 ? w : 72);
    }

    /// Rows of detail text visible inside the window.
    std::size_t detail_height() const
    {
        const int h = m_box_.y_max - m_box_.y_min + 1;
        const int rows = h - 4;
        return static_cast<std::size_t>(rows > 1 ? rows : 18);
    }

    /// The list body: the polled table plus the persistence / refresh note,
    /// with the cancel confirm dialog layered over it when open.
    ftxui::Element render_list()
    {
        using namespace ftxui;
        const std::size_t width = static_cast<std::size_t>(
            measured_width() > 1 ? measured_width() : k_default_width);
        const bool failed = !m_last_error_.empty();
        const std::string note = failed
            ? "! refresh failed (" + m_last_error_ + ")"
            : "jobs are not persistent (in-memory; a restart clears "
              "them)";
        Element note_el = text(fit(note, width))
            | to_decorator(failed ? m_theme_.error : m_theme_.hint);
        Element body =
            vbox({m_table_view_->Render() | flex, std::move(note_el)});
        if (m_flow_.awaiting_confirm()) {
            body = dbox({std::move(body), m_flow_.render_overlay()});
        }
        return body;
    }

    /// The detail body: the full job envelope in a scrollable, sanitized
    /// viewer.
    ftxui::Element render_detail()
    {
        using namespace ftxui;
        const std::size_t width = detail_width();
        Elements lines;
        if (m_detail_view_) {
            for (const std::string& line :
                 m_detail_view_->visible_lines(detail_height()))
            {
                lines.push_back(text(truncate_to_width(line, width)));
            }
        }
        if (lines.empty()) {
            lines.push_back(text("loading...") | to_decorator(m_theme_.hint));
        }
        std::string footer = "j/k scroll  r refresh  Esc back";
        if (!m_detail_terminal_) {
            footer += "   (auto-refresh)";
        }
        if (m_detail_view_ && m_detail_view_->truncated()) {
            footer += "   (truncated)";
        }
        Element content = vbox({
            vbox(std::move(lines)) | flex,
            separator(),
            text(footer) | to_decorator(m_theme_.hint),
        });
        const std::string title = " job " + fit(m_detail_id_, 40) + " ";
        return window(text(title) | to_decorator(m_theme_.title),
                      std::move(content));
    }

    ///< Alive guard shared with every async handler and the ticker; cleared by
    ///< this object's destruction so a late task never touches a dead screen
    ///< (T-120). Declared first so it outlives every other member.
    std::shared_ptr<bool> m_alive_;

    ///< Screen context (caller, session, router, options); not owned.
    ScreenContext& m_context_;

    ///< Wall clock backing the toast queue expiry.
    SystemClock m_clock_;

    ///< Toasts (cancel confirm result, no-op notices); rendered over the body.
    ToastQueue m_toasts_;

    ///< Confirm policy for the cancel action (honors `--yes`).
    ConfirmPolicy m_policy_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< The `job.cancel` descriptor (mutating forced) for the confirm/read-only
    ///< gate.
    CommandInfo m_cancel_info_;

    ///< The Jobs table/selection model.
    JobsModel m_model_;

    ///< The confirm/submit choke point for `job.cancel`.
    MutationFlow m_flow_;

    ///< Source of correlation ids for the screen's requests.
    RequestIdGenerator m_ids_;

    ///< Poll cadence and error backoff.
    PollSchedule m_schedule_;

    ///< The reusable table body over @ref m_model_'s table.
    ftxui::Component m_table_view_;

    ///< Current screen state.
    Mode m_mode_ = Mode::list;

    ///< The job id whose detail is open; empty in list mode.
    std::string m_detail_id_;

    ///< The detail envelope viewer, or null before the first `job.get` answers.
    std::unique_ptr<ResultViewerModel> m_detail_view_;

    ///< `true` once the open detail's job is terminal (stops auto-refresh).
    bool m_detail_terminal_ = false;

    ///< `true` while a `job.get` is outstanding.
    bool m_detail_in_flight_ = false;

    ///< `true` while a `job.list` is outstanding (at most one, criterion 2).
    bool m_list_in_flight_ = false;

    ///< `true` while the Jobs tab is the active screen.
    bool m_visible_ = false;

    ///< A job id handed from Import/Export to focus once the list loads.
    std::string m_pending_focus_;

    ///< The last poll's error code wire name; empty when the last poll was ok.
    std::string m_last_error_;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;

    ///< The poll timer; declared LAST so it is destroyed (its thread joined)
    ///< before every other member (teardown invariant, T-120).
    std::unique_ptr<PollTicker> m_ticker_;
};

/**
 * @brief The Jobs screen (D8/D11 node): wraps @ref JobsComponent as an
 * @ref IScreen and forwards the active-screen lifecycle to it.
 */
class JobsScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context and @p commands.
     * @param context The screen context; captured by the component by
     * reference.
     * @param commands The catalog; moved in.
     */
    JobsScreen(ScreenContext& context, std::vector<CommandInfo> commands)
        : m_body_(ftxui::Make<JobsComponent>(context, std::move(commands)))
    {
    }

    ScreenId id() const override { return ScreenId::jobs; }

    ftxui::Component component() override { return m_body_; }

    std::vector<KeyHint> hints() const override
    {
        return {KeyHint {"Enter", "detail"},
                KeyHint {"c", "cancel"},
                KeyHint {"r", "refresh"},
                KeyHint {"Esc", "back"}};
    }

    void on_enter() override { component_impl().on_enter(); }

    void on_leave() override { component_impl().on_leave(); }

private:
    /// Narrows the stored component to its concrete type for the lifecycle
    /// hops.
    JobsComponent& component_impl()
    {
        return *static_cast<JobsComponent*>(m_body_.get());
    }

    ///< The Jobs body component.
    ftxui::Component m_body_;
};

}  // namespace

std::unique_ptr<IScreen> make_jobs_screen(ScreenContext& context,
                                          std::vector<CommandInfo> commands)
{
    return std::make_unique<JobsScreen>(context, std::move(commands));
}

}  // namespace url_shortener::tui

/**
 * @file dashboard_screen.cpp
 * @brief The Dashboard screen (D2, subtask 06.0/17): a read-only aggregation of
 * ordinary commands rendered as six independent panels - link counters, the
 * 24-hour redirect figures, a "Top slugs" bar chart, "Recent changes", the
 * running-jobs line and the backend/cache line.
 *
 * Every panel loads independently through a @ref
 * url_shortener::tui::DashboardModel so one failing request degrades only its
 * panel. Requests go through the injected @ref url_shortener::tui::AsyncCaller
 * with a cancellable token and an epoch stale-drop; a @ref
 * url_shortener::tui::PollTicker refreshes the batch every 5 s while the tab is
 * visible and is stopped on leave. Every rendered server string is passed
 * through `sanitize_for_terminal` before width fitting (threat model T-110).
 * The screen issues only read commands (success criterion 3). Handlers capture
 * a `weak_ptr` alive-guard, never a raw screen pointer (T-120).
 */
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/screens/dashboard_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/dashboard_model.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/link_detail_model.hpp>  // format_count (reused, 13)
#include <url_shortener/tui/poll_ticker.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

namespace url_shortener::tui
{

namespace
{

/// The 80-column default before the body box is measured.
constexpr int k_default_width = 80;

/// Auto-refresh cadence while the Dashboard is visible (implementation step 5).
constexpr std::chrono::milliseconds k_refresh_interval {5000};

/// Columns of the left ("Top slugs") half of the two-up section.
constexpr std::size_t k_left_col = 40;

/// Sanitize (T-110) then truncate @p raw to at most @p width columns.
std::string fit(const std::string& raw, std::size_t width)
{
    return truncate_to_width(sanitize_for_terminal(raw), width);
}

/// Formats a double with one decimal, e.g. `99.2`; locale-independent.
std::string one_decimal(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return std::string(buffer);
}

/**
 * @brief The Dashboard body (subtask 06.0/17): six independent read-only panels
 * driven by a @ref DashboardModel, auto-refreshed by a @ref PollTicker.
 */
class DashboardComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the component over @p context and the command catalog.
     * @param context The screen context; its caller/router/options are owned by
     * @ref App and captured by reference, so they must outlive this component.
     * @param commands The catalog, read only to feature-detect `stats.summary`;
     * moved in.
     */
    DashboardComponent(ScreenContext& context,
                       std::vector<CommandInfo> commands)
        : m_alive_(std::make_shared<bool>(true))
        , m_context_(context)
        , m_theme_(theme_for(context.options.theme))
        , m_model_(has_stats_summary(commands))
    {
        m_ticker_ = std::make_unique<PollTicker>(
            m_context_.caller.poster(),
            [this, guard = std::weak_ptr<bool>(m_alive_)]
            {
                if (guard.expired()) {
                    return;  // the screen is gone; never touch it (T-120)
                }
                on_tick();
            });
        m_ticker_->set_interval(k_refresh_interval);
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        Element win = window(text(" Dashboard ") | to_decorator(m_theme_.title),
                             render_body());
        if (m_theme_.ascii_only) {
            win = ascii_filter(std::move(win));
        }
        return win | reflect(m_box_);
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        using namespace ftxui;
        if (event == Event::Character("r")) {
            refresh();
            return true;
        }
        if (event == Event::Character("j") || event == Event::ArrowDown) {
            m_selection_ = Target::jobs;
            return true;
        }
        if (event == Event::Character("k") || event == Event::ArrowUp) {
            m_selection_ = Target::links;
            return true;
        }
        if (event == Event::Return) {
            // Jump to the related tab (D11: Dashboard -> Links / Jobs). The
            // router drives the App's active screen (see Import/Export's
            // navigate_tab hand-off).
            m_context_.router.navigate_tab(m_selection_ == Target::jobs ? '5'
                                                                        : '2');
            return true;
        }
        return false;
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

    /// Called when the Dashboard tab becomes active: load every panel and start
    /// the 5 s auto-refresh.
    void on_enter()
    {
        m_visible_ = true;
        refresh();
        m_ticker_->start();
    }

    /// Called when the Dashboard tab stops being active: cancel outstanding
    /// requests and stop the auto-refresh so the app does not poll on another
    /// tab (every polling bounded, stopped when not visible).
    void on_leave()
    {
        m_visible_ = false;
        cancel_pending();
        m_ticker_->stop();
    }

private:
    /// The two Enter-jump targets (implementation step 5).
    enum class Target
    {
        links,  ///< Jump to the Links tab (`2`).
        jobs  ///< Jump to the Jobs tab (`5`).
    };

    /// Reports whether the catalog offers the (proposed) `stats.summary`
    /// command, selecting the exact-metrics request set.
    static bool has_stats_summary(const std::vector<CommandInfo>& commands)
    {
        for (const CommandInfo& info : commands) {
            if (info.name == "stats.summary") {
                return true;
            }
        }
        return false;
    }

    /// Cancels every outstanding request of the current batch so a late
    /// response can never reach the model (T-120).
    void cancel_pending()
    {
        for (const CallToken token : m_tokens_) {
            m_context_.caller.cancel(token);
        }
        m_tokens_.clear();
        m_pending_ = 0;
    }

    /// Submits a fresh batch unless one is still outstanding (bounded: at most
    /// one batch in flight). Each request carries the current epoch so a stale
    /// response from a superseded batch is dropped.
    void refresh()
    {
        if (m_pending_ > 0) {
            return;  // a batch is already in flight; do not pile up
        }
        ++m_epoch_;
        m_model_.start_refresh();
        const std::uint64_t epoch = m_epoch_;
        const std::weak_ptr<bool> guard = m_alive_;
        for (commands::CommandRequest& request :
             m_model_.refresh_requests([this] { return m_ids_.next(); }))
        {
            const std::string panel = DashboardModel::panel_of(request.id);
            // Review A: a request the session may not run (link.list status=all
            // / deleted raises to write, D16) is never issued in a read-only
            // session; its panel renders `n/a (read-only)` instead.
            CommandInfo probe;
            probe.name = request.command;
            probe.permission = "read";
            if (!capability_for(m_context_)
                     .can_run(probe, request.args_json)
                     .allowed)
            {
                m_model_.mark_restricted(panel);
                continue;
            }
            ++m_pending_;
            const CallToken token = m_context_.caller.submit(
                std::move(request),
                [this, guard, epoch, panel](
                    const commands::CommandResponse& response)
                {
                    if (guard.expired()) {
                        return;  // the screen is gone (T-120)
                    }
                    if (epoch != m_epoch_) {
                        return;  // a superseded batch (defense in depth)
                    }
                    if (m_pending_ > 0) {
                        --m_pending_;
                    }
                    m_model_.apply(panel, response);
                });
            m_tokens_.push_back(token);
        }
    }

    /// The periodic tick (UI thread): auto-refresh while visible.
    void on_tick()
    {
        if (m_visible_) {
            refresh();
        }
    }

    /// The inner content width from the last frame, or the 80-column guess.
    std::size_t inner_width() const
    {
        const int w = m_box_.x_max - m_box_.x_min + 1 - 2;  // minus borders
        return static_cast<std::size_t>(w > 8 ? w : k_default_width - 2);
    }

    /// The full window body: counts, redirects, the two-up section, the status
    /// lines and the footer hint.
    ftxui::Element render_body()
    {
        using namespace ftxui;
        const std::size_t width = inner_width();
        return vbox({
            counts_line(width),
            redirects_line(width),
            text(""),
            two_up_section(width),
            filler(),
            separator(),
            status_line(width),
            footer_line(width),
        });
    }

    /// The link-counts line (D2 "Links total/active/disabled/deleted").
    ftxui::Element counts_line(std::size_t width)
    {
        using namespace ftxui;
        const CountsPanel& c = m_model_.counts();
        if (c.state == PanelState::loading) {
            return text(fit("Links  loading...", width))
                | to_decorator(m_theme_.hint);
        }
        if (c.state == PanelState::unavailable) {
            return text(fit("Links  n/a (" + m_model_.counts_error() + ")",
                            width))
                | to_decorator(m_theme_.error);
        }
        // Review A: a count withheld in a read-only session (status=all /
        // deleted raises to write) shows `n/a (read-only)`, not a stale 0.
        const std::string total =
            c.total_restricted ? "n/a (read-only)" : format_count(c.total);
        const std::string deleted =
            c.deleted_restricted ? "n/a (read-only)" : format_count(c.deleted);
        std::string line = "Links  total: " + total + "  active: "
            + format_count(c.active) + "  disabled: " + format_count(c.disabled)
            + "  deleted: " + deleted;
        if (!m_model_.persistent()) {
            line += "  (this session)";
        }
        return text(fit(line, width));
    }

    /// The redirect-metrics line (D2 "Redirects (24h) / success / p50").
    ftxui::Element redirects_line(std::size_t width)
    {
        using namespace ftxui;
        const RedirectsPanel& r = m_model_.redirects();
        // Review A: the sample feed is link.list {status:all}, which raises to
        // a write; a read-only session withholds it and shows n/a (read-only).
        if (m_model_.restricted("redirects")) {
            return text(fit("Redirects (24h): n/a (read-only)", width))
                | to_decorator(m_theme_.hint);
        }
        if (r.state == PanelState::loading) {
            return text(fit("Redirects (24h): loading...", width))
                | to_decorator(m_theme_.hint);
        }
        if (r.state == PanelState::unavailable) {
            return text(fit("Redirects (24h): n/a ("
                                + m_model_.redirects_error() + ")",
                            width))
                | to_decorator(m_theme_.error);
        }
        std::string line = "Redirects (24h): "
            + (r.redirects_24h.has_value() ? format_count(*r.redirects_24h)
                                           : std::string("n/a"));
        if (r.sampled) {
            line += " (sample)";
        }
        // The lifetime click-events total (db.tables) is the one exact redirect
        // figure; shown as context next to the sampled 24h estimate.
        if (m_model_.click_events().has_value()) {
            line += "   events: " + format_count(*m_model_.click_events());
        }
        line += "   success: "
            + (r.success_pct.has_value() ? one_decimal(*r.success_pct) + "%"
                                         : std::string("n/a"));
        line += "   p50: "
            + (r.p50_ms.has_value() ? one_decimal(*r.p50_ms) + " ms"
                                    : std::string("n/a"));
        return text(fit(line, width));
    }

    /// The two-up "Top slugs" / "Recent changes" section.
    ftxui::Element two_up_section(std::size_t width)
    {
        using namespace ftxui;
        const std::size_t left = width > k_left_col ? k_left_col : width;
        const std::size_t right = width > left ? width - left : 0;
        return hbox({
            vbox(top_slugs_lines(left)),
            vbox(recent_lines(right)),
        });
    }

    /// The left column: the "Top slugs" header and up to three bar rows.
    std::vector<ftxui::Element> top_slugs_lines(std::size_t width)
    {
        using namespace ftxui;
        const RedirectsPanel& r = m_model_.redirects();
        std::string header = "Top slugs (24h)";
        if (r.sampled) {
            header += " (sample of 200)";
        }
        // Every left-column line is padded to the full column width so the
        // Recent-changes column starts at a fixed offset (a visible gap).
        const auto pad_col = [width](const std::string& raw)
        { return pad_to_width(fit(raw, width), width); };

        std::vector<Element> lines;
        lines.push_back(text(pad_col(header))
                        | to_decorator(m_theme_.header_row));
        if (m_model_.restricted("redirects")) {
            lines.push_back(text(pad_col("  n/a (read-only)"))
                            | to_decorator(m_theme_.hint));
            return lines;
        }
        if (r.state == PanelState::loading) {
            lines.push_back(text(pad_col("  loading..."))
                            | to_decorator(m_theme_.hint));
            return lines;
        }
        if (r.state == PanelState::unavailable) {
            lines.push_back(text(pad_col("  n/a"))
                            | to_decorator(m_theme_.error));
            return lines;
        }
        const std::vector<TopSlug>& top = m_model_.top_slugs();
        if (top.empty()) {
            lines.push_back(text(pad_col("  (no redirects)"))
                            | to_decorator(m_theme_.hint));
            return lines;
        }
        const std::uint64_t max = top.front().count;
        for (const TopSlug& slug : top) {
            const std::string row = "  " + pad_to_width(fit(slug.slug, 10), 10)
                + " " + pad_to_width(format_count(slug.count), 7, true) + " "
                + m_model_.bars(slug.count, max, 8, m_theme_.ascii_only);
            lines.push_back(
                text(pad_to_width(truncate_to_width(row, width), width)));
        }
        return lines;
    }

    /// The right column: the "Recent changes" header and up to five rows.
    std::vector<ftxui::Element> recent_lines(std::size_t width)
    {
        using namespace ftxui;
        std::vector<Element> lines;
        lines.push_back(text(fit("Recent changes", width))
                        | to_decorator(m_theme_.header_row));
        if (m_model_.restricted("recent")) {
            lines.push_back(text(fit("  n/a (read-only)", width))
                            | to_decorator(m_theme_.hint));
            return lines;
        }
        const PanelState state = m_model_.recent_state();
        if (state == PanelState::loading) {
            lines.push_back(text(fit("  loading...", width))
                            | to_decorator(m_theme_.hint));
            return lines;
        }
        if (state == PanelState::unavailable) {
            lines.push_back(text(fit("  n/a", width))
                            | to_decorator(m_theme_.error));
            return lines;
        }
        const std::vector<RecentChange>& recent = m_model_.recent();
        if (recent.empty()) {
            lines.push_back(text(fit("  (none)", width))
                            | to_decorator(m_theme_.hint));
            return lines;
        }
        for (const RecentChange& change : recent) {
            const std::string row = fit(change.time_hhmm, 5) + " "
                + pad_to_width(fit(change.slug, 12), 12) + " "
                + fit(change.status, 10);
            lines.push_back(text(truncate_to_width(row, width)));
        }
        return lines;
    }

    /// The jobs/backend status line (D2 bottom row).
    ftxui::Element status_line(std::size_t width)
    {
        using namespace ftxui;
        const std::size_t half = width / 2;
        return hbox({
            text(fit(m_model_.jobs_line(), half)),
            filler(),
            text(fit(m_model_.backend_line(), width - half)),
        });
    }

    /// The footer hint line (the Enter target reflects the current selection).
    ftxui::Element footer_line(std::size_t width)
    {
        using namespace ftxui;
        const std::string target =
            m_selection_ == Target::jobs ? "Jobs" : "Links";
        const std::string hint =
            "[Enter] open " + target + "   [r] refresh   [j/k] select";
        return text(fit(hint, width)) | to_decorator(m_theme_.hint);
    }

    ///< Alive guard shared with every async handler and the ticker; cleared by
    ///< this object's destruction so a late task never touches a dead screen
    ///< (T-120). Declared first so it outlives every other member.
    std::shared_ptr<bool> m_alive_;

    ///< Screen context (caller, router, options); not owned.
    ScreenContext& m_context_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< The six-panel view-model fed by the request batches.
    DashboardModel m_model_;

    ///< Source of correlation ids for the screen's requests.
    RequestIdGenerator m_ids_;

    ///< Tokens of the current batch's in-flight requests; cancelled on leave /
    ///< refresh.
    std::vector<CallToken> m_tokens_;

    ///< Monotonic batch epoch; a completion whose epoch differs is dropped.
    std::uint64_t m_epoch_ = 0;

    ///< Number of current-batch requests not yet delivered; `0` means idle, so
    ///< a refresh may start (bounds to at most one batch in flight).
    std::size_t m_pending_ = 0;

    ///< `true` while the Dashboard tab is the active screen.
    bool m_visible_ = false;

    ///< The current Enter-jump target.
    Target m_selection_ = Target::links;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;

    ///< The 5 s auto-refresh timer; declared LAST so it is destroyed (its
    ///< thread joined) before every other member (teardown invariant, T-120).
    std::unique_ptr<PollTicker> m_ticker_;
};

/**
 * @brief The Dashboard screen (D2 node): wraps @ref DashboardComponent as an
 * @ref IScreen and forwards the active-screen lifecycle to it.
 */
class DashboardScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context and @p commands.
     * @param context The screen context; captured by the component by
     * reference.
     * @param commands The catalog; moved in.
     */
    DashboardScreen(ScreenContext& context, std::vector<CommandInfo> commands)
        : m_body_(ftxui::Make<DashboardComponent>(context, std::move(commands)))
    {
    }

    ScreenId id() const override { return ScreenId::dashboard; }

    ftxui::Component component() override { return m_body_; }

    std::vector<KeyHint> hints() const override
    {
        return {KeyHint {"Enter", "open"},
                KeyHint {"r", "refresh"},
                KeyHint {"j/k", "select"}};
    }

    void on_enter() override { component_impl().on_enter(); }

    void on_leave() override { component_impl().on_leave(); }

private:
    /// Narrows the stored component to its concrete type for the lifecycle
    /// hops.
    DashboardComponent& component_impl()
    {
        return *static_cast<DashboardComponent*>(m_body_.get());
    }

    ///< The Dashboard body component.
    ftxui::Component m_body_;
};

}  // namespace

std::unique_ptr<IScreen> make_dashboard_screen(
    ScreenContext& context, std::vector<CommandInfo> commands)
{
    return std::make_unique<DashboardScreen>(context, std::move(commands));
}

}  // namespace url_shortener::tui

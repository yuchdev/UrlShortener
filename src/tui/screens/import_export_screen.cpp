/**
 * @file import_export_screen.cpp
 * @brief The Import / Export screen (D7, subtask 06.0/15): a bespoke form with
 * an Import sub-tab (`link.import`) and an Export sub-tab (`link.export`), both
 * job-backed. Implements the D11 edge `ImportExport -> Jobs (Start, job
 * accepted)`.
 *
 * Every submit routes through @ref url_shortener::tui::run_mutation with the
 * session read-only flag and the shared @ref ConfirmPolicy decision (ADR 0003
 * rule 4, threat model T-116): `link.import` is a write (confirmed unless
 * `--yes`, refused in read-only), `link.export` is a read (submitted directly)
 * whose `path` the server raises to a write and refuses in read-only - so the
 * export `path` field renders disabled in a read-only session. The screen
 * issues no command through @ref ICommandClient directly (contract C8), builds
 * no JSON itself (the @ref ImportModel / @ref ExportModel do, contract C1/C9),
 * opens no file (a `path` is a command argument only, T-118) and sanitizes
 * every server/stored string before display (threat model T-110). A
 * path/capability denial is surfaced as a value-free toast (the server's
 * `error.code` + message, never the path bytes).
 */
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/screens/import_export_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/async_caller.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/import_model.hpp>
#include <url_shortener/tui/links_query.hpp>
#include <url_shortener/tui/mutation_flow.hpp>
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

/// Visible rows of the import paste area.
constexpr std::size_t k_paste_rows = 5;

/// Byte cap for a single-line text field (path / q / tag / campaign); the
/// server applies the authoritative per-argument limit.
constexpr std::size_t k_text_field_cap = 8192;

/// Sanitize (T-110) then truncate @p raw to at most @p width columns.
std::string fit(const std::string& raw, std::size_t width)
{
    return truncate_to_width(sanitize_for_terminal(raw), width);
}

/// The focusable controls of either sub-tab, in visual (Tab) order.
enum class Ctl
{
    // Import sub-tab.
    src,  ///< Source radio (paste / file path).
    imp_path,  ///< Import file-path text field.
    fmt,  ///< Import format radio (CSV / NDJSON).
    conflict,  ///< On-conflict radio (skip / fail / overwrite).
    paste,  ///< Multi-line paste area.
    dry_run,  ///< Dry-run (validate only) checkbox.
    start,  ///< Start import button.
    cancel,  ///< Cancel (reset) button.
    // Export sub-tab.
    exp_fmt,  ///< Export format radio (CSV / NDJSON).
    status,  ///< Export status filter radio.
    exp_q,  ///< Export free-text query field.
    exp_tag,  ///< Export tag filter field.
    exp_camp,  ///< Export campaign filter field.
    exp_path,  ///< Export file-path text field (disabled in read-only).
    exp_start  ///< Start export button.
};

/// Whether @p c edits a text buffer (so printable keys/Backspace go to it).
bool is_text(Ctl c)
{
    return c == Ctl::imp_path || c == Ctl::paste || c == Ctl::exp_q
        || c == Ctl::exp_tag || c == Ctl::exp_camp || c == Ctl::exp_path;
}

/// Whether @p c is a radio (Left/Right/Space cycle its value).
bool is_radio(Ctl c)
{
    return c == Ctl::src || c == Ctl::fmt || c == Ctl::conflict
        || c == Ctl::exp_fmt || c == Ctl::status;
}

/// Whether @p c is a button (Enter/Space activate it).
bool is_button(Ctl c)
{
    return c == Ctl::start || c == Ctl::cancel || c == Ctl::exp_start;
}

/// Flips a two-state format radio.
ImportFormat other_format(ImportFormat f)
{
    return f == ImportFormat::csv ? ImportFormat::ndjson : ImportFormat::csv;
}

/**
 * @brief The Import / Export body: two job-backed forms over one mutation
 * choke point (subtask 06.0/15).
 */
class ImportExportComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the component over @p context and @p catalog.
     * @param context The screen context; its caller/router are references owned
     * by @ref App that must outlive this component (captured by reference).
     * @param catalog The command catalog, used to resolve the `link.import` /
     * `link.export` @ref CommandInfo each submit needs; moved in.
     */
    ImportExportComponent(ScreenContext& context,
                          std::vector<CommandInfo> catalog)
        : m_alive_(std::make_shared<bool>(true))
        , m_context_(context)
        , m_catalog_(std::move(catalog))
        , m_theme_(theme_for(context.options.theme))
        , m_toasts_(m_clock_)
        , m_policy_(context.options.assume_yes)
        , m_flow_(MutationDeps {context.caller,
                                m_policy_,
                                m_toasts_,
                                m_theme_,
                                capability_for(context)})
        , m_import_(context.options.max_inline_bytes)
    {
        // Compute the default focus WITHOUT touching the shared
        // ScreenContext::text_input_active (BUG-2, 06.0/20 smoke): this
        // constructor runs for every registered screen at startup, including
        // while this screen is off-screen, so writing the shared flag here
        // would leak Import/Export's text focus onto whatever tab is active.
        // The flag is (re)asserted from on_enter once this screen is shown.
        compute_default_focus();
        // Review A (display): the all/deleted export views raise link.export to
        // a write (D16), so a read-only session cannot query them. Default to
        // active rather than offering a view the server would refuse.
        if (!status_queryable(StatusFilter::all)) {
            m_export_status_ = StatusFilter::active;
        }
    }

    /// Called when the tab becomes active: ensure the focus is valid.
    void on_enter() { reset_focus(); }

    /// Called when the tab is left: drop the transient inline message so it
    /// cannot reappear behind another tab.
    void on_leave() { m_inline_message_.clear(); }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        m_toasts_.tick();
        Element body =
            m_mode_ == Mode::import_tab ? render_import() : render_export();
        if (m_flow_.awaiting_confirm()) {
            body = dbox({std::move(body), m_flow_.render_overlay()});
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
        if (m_flow_.awaiting_confirm()) {
            const bool handled = m_flow_.overlay_event(event);
            update_text_input_active();
            return handled;  // the confirm is modal
        }
        if (event == Event::F2) {
            toggle_mode();
            return true;
        }
        if (event == Event::CtrlS) {
            submit();
            update_text_input_active();
            return true;
        }
        if (event == Event::Tab) {
            move_focus(1);
            return true;
        }
        if (event == Event::TabReverse) {
            move_focus(-1);
            return true;
        }

        const Ctl cur = current_control();
        if (is_text(cur)) {
            if (handle_text_event(cur, event)) {
                return true;
            }
            if (event == Event::Return) {
                move_focus(1);  // Enter in a single-line field advances focus
                return true;
            }
            return false;
        }
        if (is_radio(cur)) {
            if (event == Event::ArrowLeft) {
                cycle(cur, -1);
                return true;
            }
            if (event == Event::ArrowRight || event == Event::Character(" ")
                || event == Event::Return)
            {
                cycle(cur, 1);
                return true;
            }
            return false;
        }
        if (cur == Ctl::dry_run) {
            if (event == Event::Character(" ") || event == Event::Return) {
                m_import_.set_dry_run(!m_import_.dry_run());
                return true;
            }
            return false;
        }
        if (is_button(cur)) {
            if (event == Event::Return || event == Event::Character(" ")) {
                activate_button(cur);
                update_text_input_active();
                return true;
            }
            return false;
        }
        return false;
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

private:
    ///< The two sub-tabs.
    enum class Mode
    {
        import_tab,
        export_tab
    };

    // --- Focus ---------------------------------------------------------------

    /// The focusable controls of the active sub-tab, in Tab order. The export
    /// `path` field is omitted in a read-only session (it renders disabled).
    std::vector<Ctl> controls() const
    {
        if (m_mode_ == Mode::import_tab) {
            return {Ctl::src,
                    Ctl::imp_path,
                    Ctl::fmt,
                    Ctl::conflict,
                    Ctl::paste,
                    Ctl::dry_run,
                    Ctl::start,
                    Ctl::cancel};
        }
        std::vector<Ctl> v = {
            Ctl::exp_fmt, Ctl::status, Ctl::exp_q, Ctl::exp_tag, Ctl::exp_camp};
        // The path field raises link.export to a write (D16); omit it from the
        // focus ring when the session cannot write it (subtask 18).
        if (!writes_denied()) {
            v.push_back(Ctl::exp_path);
        }
        v.push_back(Ctl::exp_start);
        return v;
    }

    /// The control the focus index currently points at (clamped).
    Ctl current_control() const
    {
        const std::vector<Ctl> c = controls();
        const std::size_t i = m_focus_ < c.size() ? m_focus_ : c.size() - 1;
        return c[i];
    }

    /// Sets @ref m_focus_ to the sub-tab default (the paste area on Import, the
    /// format radio on Export) without touching the shared context, so it is
    /// safe to call from the constructor while this screen is off-screen.
    void compute_default_focus()
    {
        if (m_mode_ == Mode::import_tab) {
            const std::vector<Ctl> c = controls();
            m_focus_ = 0;
            for (std::size_t i = 0; i < c.size(); ++i) {
                if (c[i] == Ctl::paste) {
                    m_focus_ = i;
                    break;
                }
            }
        }
        else {
            m_focus_ = 0;
        }
    }

    /// Resets the focus to the sub-tab default and refreshes the text-input
    /// flag. Only call this on the active screen (e.g. from @ref on_enter or an
    /// interaction handler); the constructor uses @ref compute_default_focus
    /// instead so an off-screen screen never writes the shared flag.
    void reset_focus()
    {
        compute_default_focus();
        update_text_input_active();
    }

    /// Moves the focus by @p delta within the current control ring (wrapping).
    void move_focus(int delta)
    {
        const std::vector<Ctl> c = controls();
        const int n = static_cast<int>(c.size());
        if (n == 0) {
            return;
        }
        int next = static_cast<int>(m_focus_ < c.size() ? m_focus_ : 0);
        next = ((next + delta) % n + n) % n;
        m_focus_ = static_cast<std::size_t>(next);
        update_text_input_active();
    }

    /// Reflects into the shared context whether a text field (or a confirm) has
    /// focus, so the global hotkey handler ignores digits/`:`/`q` while typing.
    void update_text_input_active()
    {
        m_context_.text_input_active =
            m_flow_.awaiting_confirm() || is_text(current_control());
    }

    /// Switches sub-tab, resetting the transient state and focus.
    void toggle_mode()
    {
        m_mode_ =
            m_mode_ == Mode::import_tab ? Mode::export_tab : Mode::import_tab;
        m_inline_message_.clear();
        reset_focus();
    }

    // --- Text editing --------------------------------------------------------

    /// The editable buffer behind a text control.
    std::string& text_buffer(Ctl c)
    {
        switch (c) {
            case Ctl::imp_path:
                return m_imp_path_;
            case Ctl::exp_q:
                return m_q_;
            case Ctl::exp_tag:
                return m_tag_;
            case Ctl::exp_camp:
                return m_campaign_;
            case Ctl::exp_path:
                return m_exp_path_;
            case Ctl::paste:
            default:
                return m_paste_;
        }
    }

    /// Deletes the last UTF-8 code point from @p buf; returns `true` iff @p buf
    /// changed (so a Backspace on an empty field does no per-edit work).
    static bool utf8_backspace(std::string& buf)
    {
        if (buf.empty()) {
            return false;
        }
        std::size_t cut = buf.size() - 1;
        while (cut > 0
               && (static_cast<unsigned char>(buf[cut]) & 0xC0u) == 0x80u)
        {
            --cut;
        }
        buf.erase(cut);
        return true;
    }

    /// Routes one keyboard event to the focused text buffer; returns `true`
    /// when consumed.
    bool handle_text_event(Ctl c, const ftxui::Event& event)
    {
        using namespace ftxui;
        std::string& buf = text_buffer(c);
        const std::size_t cap = c == Ctl::paste
            ? m_import_.max_inline_bytes() + 1
            : k_text_field_cap;
        if (event == Event::Backspace) {
            if (utf8_backspace(buf)) {
                after_text_edit(c);
            }
            return true;
        }
        // A character/newline that does not fit (buffer already at the cap)
        // leaves the buffer unchanged, so the per-edit preview/sync is skipped
        // - a multi-MB paste past the cap stays O(cap), not O(N) (CWE-407).
        if (c == Ctl::paste && event == Event::Return) {
            if (append_within_cap(buf, "\n", cap)) {
                after_text_edit(c);
            }
            return true;
        }
        if (event.is_character()) {
            if (append_within_cap(buf, event.character(), cap)) {
                after_text_edit(c);
            }
            return true;
        }
        return false;
    }

    /// Syncs a changed buffer into its model and refreshes the cached preview.
    void after_text_edit(Ctl c)
    {
        m_inline_message_.clear();
        if (c == Ctl::paste) {
            m_import_.set_text(m_paste_);
            m_preview_ = m_import_.preview();
        }
    }

    // --- Radio cycling -------------------------------------------------------

    /// Cycles the focused radio by @p dir (+1 forward, -1 backward).
    void cycle(Ctl c, int dir)
    {
        m_inline_message_.clear();
        switch (c) {
            case Ctl::src:
                m_import_.set_source(m_import_.source() == ImportSource::paste
                                         ? ImportSource::file
                                         : ImportSource::paste);
                break;
            case Ctl::fmt:
                m_import_.set_format(other_format(m_import_.format()));
                break;
            case Ctl::exp_fmt:
                m_export_.set_format(other_format(m_export_.format()));
                break;
            case Ctl::conflict:
                cycle_conflict(dir);
                break;
            case Ctl::status:
                cycle_status(dir);
                break;
            default:
                break;
        }
    }

    /// Advances the on-conflict radio skip -> fail -> overwrite (wrapping).
    void cycle_conflict(int dir)
    {
        const int n = 3;
        int idx = static_cast<int>(m_import_.on_conflict());
        idx = ((idx + dir) % n + n) % n;
        m_import_.set_on_conflict(static_cast<OnConflict>(idx));
    }

    /// Whether this session is denied writes - the one capability decision
    /// (subtask 18). In a read-only session link.import, path export and the
    /// all/deleted export views are all refused.
    bool writes_denied() const
    {
        CommandInfo write_probe;
        write_probe.permission = "write";
        return !capability_for(m_context_).can_run(write_probe).allowed;
    }

    /// Whether this session may run an export of the given status view: the
    /// all/deleted views raise link.export to a write (D16), so a read-only
    /// session cannot. Arg-aware via the one capability decision.
    bool status_queryable(StatusFilter status) const
    {
        CommandInfo exp;
        exp.name = "link.export";
        exp.permission = "read";
        const std::string args =
            "{\"status\":\"" + status_to_wire(status) + "\"}";
        return capability_for(m_context_).can_run(exp, args).allowed;
    }

    /// The next export status in cyclic radio order for @p dir.
    static StatusFilter step_status(StatusFilter status, int dir)
    {
        const int n = 4;
        int idx = static_cast<int>(status);
        idx = ((idx + dir) % n + n) % n;
        return static_cast<StatusFilter>(idx);
    }

    /// Advances the export status radio, skipping views this session cannot
    /// query (all/deleted in a read-only session; review A display).
    void cycle_status(int dir)
    {
        StatusFilter candidate = step_status(m_export_status_, dir);
        for (int i = 0; i < 4 && !status_queryable(candidate); ++i) {
            candidate = step_status(candidate, dir);
        }
        m_export_status_ = candidate;
    }

    // --- Submit --------------------------------------------------------------

    /// Activates the button @p c (Start submits; Cancel resets the form).
    void activate_button(Ctl c)
    {
        if (c == Ctl::cancel) {
            reset_import();
            return;
        }
        submit();
    }

    /// Clears the Import form back to its defaults.
    void reset_import()
    {
        m_paste_.clear();
        m_imp_path_.clear();
        m_import_ = ImportModel(m_context_.options.max_inline_bytes);
        m_preview_ = ImportModel::Preview {};
        m_inline_message_.clear();
        reset_focus();
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

    /// Submits the active sub-tab's form through the mutation choke point.
    void submit()
    {
        if (m_flow_.awaiting_confirm() || m_flow_.busy()) {
            return;
        }
        if (m_mode_ == Mode::import_tab) {
            submit_import();
        }
        else {
            submit_export();
        }
    }

    /// Submits `link.import`: a blocking error short-circuits to an inline
    /// message (no request); otherwise the request routes through
    /// @ref run_mutation (confirm + read-only gate).
    void submit_import()
    {
        m_import_.set_text(m_paste_);
        m_import_.set_path(m_imp_path_);
        const std::optional<std::string> blocked = m_import_.blocking_error();
        if (blocked.has_value()) {
            m_inline_message_ = *blocked;  // success criterion 4: never submits
            return;
        }
        const CommandInfo* info = find_command("link.import");
        if (info == nullptr) {
            toast_unavailable("link.import");
            return;
        }
        m_inline_message_.clear();
        run_mutation(m_flow_,
                     m_import_.to_request(m_ids_.next()),
                     *info,
                     make_on_result());
        update_text_input_active();
    }

    /// Submits `link.export`: a read command (so no confirm), whose `path` the
    /// server raises to a write and refuses in read-only. The path field is
    /// omitted from the request in read-only (it is not editable there).
    void submit_export()
    {
        LinkListFilter filter;
        filter.status = m_export_status_;
        filter.q = m_q_;
        filter.tag = m_tag_;
        filter.campaign = m_campaign_;
        m_export_.set_filter(filter);
        m_export_.set_path(writes_denied() ? std::string() : m_exp_path_);
        const CommandInfo* info = find_command("link.export");
        if (info == nullptr) {
            toast_unavailable("link.export");
            return;
        }
        m_inline_message_.clear();
        run_mutation(m_flow_,
                     m_export_.to_request(m_ids_.next()),
                     *info,
                     make_on_result());
    }

    /// The shared submit result handler: on a job response hand the job to the
    /// Jobs screen and switch to the Jobs tab (D7 "after Start -> Jobs"). The
    /// success/error/job toast is pushed by @ref MutationFlow itself, so this
    /// only navigates and never re-toasts; on any error the form data is kept.
    std::function<void(const commands::CommandResponse&)> make_on_result()
    {
        std::weak_ptr<bool> guard = m_alive_;
        return [this, guard](const commands::CommandResponse& resp)
        {
            if (guard.expired()) {
                return;  // the screen is gone; never touch it (T-120)
            }
            if (resp.ok && resp.job.has_value()) {
                m_context_.focus_job_id = resp.job->id;
                m_context_.router.navigate_tab('5');  // D11: -> Jobs
            }
        };
    }

    /// Raises a fixed "command unavailable" error toast (value-free).
    void toast_unavailable(const std::string& command)
    {
        Toast t;
        t.kind = ToastKind::error;
        t.title = "! not_available";
        t.body = "command unavailable in this build";
        t.source_command = command;
        m_toasts_.push(std::move(t));
    }

    // --- Rendering -----------------------------------------------------------

    /// The inner content width from the last frame, or the 80-column guess.
    int measured_width() const
    {
        const int w = m_box_.x_max - m_box_.x_min + 1;
        return w > 1 ? w : k_default_width;
    }

    /// Wrapping width for text inside the window border.
    std::size_t inner_width() const
    {
        const int w = measured_width() - 4;
        return static_cast<std::size_t>(w > 8 ? w : 72);
    }

    /// The sub-tab selector line: `(Import)  Export` / `Import  (Export)`.
    ftxui::Element tab_line() const
    {
        using namespace ftxui;
        const bool imp = m_mode_ == Mode::import_tab;
        return hbox({
            text(imp ? "(Import)" : " Import ")
                | to_decorator(imp ? m_theme_.selected_row : m_theme_.hint),
            text("  "),
            text(!imp ? "(Export)" : " Export ")
                | to_decorator(!imp ? m_theme_.selected_row : m_theme_.hint),
            text("   "),
            text("F2 switch") | to_decorator(m_theme_.hint),
        });
    }

    /// A two-choice radio built only from fixed labels (review 6: no server
    /// data crosses the trusted-caller boundary).
    ftxui::Element radio2(const std::string& prefix,
                          bool first_selected,
                          const std::string& first,
                          const std::string& second) const
    {
        using namespace ftxui;
        return hbox({
            text(prefix),
            text((first_selected ? "(*) " : "( ) ") + first),
            text("   "),
            text((!first_selected ? "(*) " : "( ) ") + second),
        });
    }

    /// A single-line labelled text field: `label [<value|placeholder>]`.
    ftxui::Element text_field(const std::string& label,
                              const std::string& value,
                              const std::string& placeholder,
                              bool disabled,
                              const std::string& disabled_reason) const
    {
        using namespace ftxui;
        if (disabled) {
            // The reason must render in full (it names the error code the
            // operator needs); it is not confined to the input-field width.
            return hbox({text(label),
                         text(fit(disabled_reason, 62))
                             | to_decorator(m_theme_.disabled)});
        }
        const std::size_t field_w = 30;
        const std::string shown = value.empty() ? placeholder : value;
        Element content = text("[" + fit(shown, field_w) + "]");
        if (value.empty()) {
            content = content | to_decorator(m_theme_.hint);
        }
        return hbox({text(label), std::move(content)});
    }

    /// Renders the Import sub-tab (D7).
    ftxui::Element render_import()
    {
        using namespace ftxui;
        const std::size_t w = inner_width();
        const bool import_disabled = writes_denied();

        Elements rows;
        rows.push_back(tab_line());
        rows.push_back(text(""));

        // Source line: paste vs file path, with the path field inline.
        rows.push_back(hbox({
            text("Source:  "),
            text(m_import_.source() == ImportSource::paste ? "(*) paste"
                                                           : "( ) paste"),
            text("   "),
            text(m_import_.source() == ImportSource::file ? "(*) file path "
                                                          : "( ) file path "),
            text("[" + fit(m_imp_path_, 28) + "]")
                | to_decorator(m_imp_path_.empty() ? m_theme_.hint
                                                   : m_theme_.title),
        }));

        rows.push_back(radio2("Format:  ",
                              m_import_.format() == ImportFormat::csv,
                              "CSV  full_url,slug",
                              "NDJSON"));

        rows.push_back(conflict_line());
        rows.push_back(text(""));
        rows.push_back(paste_area(w));
        rows.push_back(preview_line(w));
        rows.push_back(text(""));
        rows.push_back(action_line(import_disabled));

        if (!m_inline_message_.empty()) {
            rows.push_back(text(fit("! " + m_inline_message_, w))
                           | to_decorator(m_theme_.error));
        }
        if (import_disabled) {
            rows.push_back(
                text(fit("read-only session: import needs write access "
                         "(not_authorized)",
                         w))
                | to_decorator(m_theme_.disabled));
        }
        rows.push_back(text(""));
        rows.push_back(text("Tab move  Space/</> set  Ctrl-S start  "
                            "F2 export")
                       | to_decorator(m_theme_.hint));

        const std::string title =
            " Import pairs (command: link.import, runs as a job) ";
        return window(text(title) | to_decorator(m_theme_.title),
                      vbox(std::move(rows)));
    }

    /// The on-conflict radio line, flagging overwrite as destructive.
    ftxui::Element conflict_line() const
    {
        using namespace ftxui;
        const OnConflict oc = m_import_.on_conflict();
        return hbox({
            text("On conflict: "),
            text(oc == OnConflict::skip ? "(*) skip" : "( ) skip"),
            text("   "),
            text(oc == OnConflict::fail ? "(*) fail" : "( ) fail"),
            text("   "),
            text(oc == OnConflict::overwrite ? "(*) overwrite (destructive)"
                                             : "( ) overwrite (destructive)"),
        });
    }

    /// The bordered multi-line paste area; shows the D7 sample as a placeholder
    /// when empty, and dims when the source is a file path.
    ftxui::Element paste_area(std::size_t w) const
    {
        using namespace ftxui;
        const bool is_file = m_import_.source() == ImportSource::file;
        Elements lines;
        if (m_paste_.empty()) {
            static const char* sample[] = {
                "https://example.com/a,a",
                "https://example.com/b,b",
                "https://example.com/c,                 <- blank slug = "
                "generated",
            };
            for (const char* s : sample) {
                lines.push_back(text(fit(s, w)) | to_decorator(m_theme_.hint));
            }
        }
        else {
            // Show the last k_paste_rows lines (the editing window).
            std::vector<std::string> buf_lines;
            std::size_t start = 0;
            while (start <= m_paste_.size()) {
                const std::size_t nl = m_paste_.find('\n', start);
                const std::size_t end =
                    nl == std::string::npos ? m_paste_.size() : nl;
                buf_lines.push_back(m_paste_.substr(start, end - start));
                if (nl == std::string::npos) {
                    break;
                }
                start = nl + 1;
            }
            const std::size_t first = buf_lines.size() > k_paste_rows
                ? buf_lines.size() - k_paste_rows
                : 0;
            for (std::size_t i = first; i < buf_lines.size(); ++i) {
                lines.push_back(text(fit(buf_lines[i], w)));
            }
        }
        Element content = vbox(std::move(lines));
        if (is_file) {
            content = content | to_decorator(m_theme_.disabled);
        }
        return content | border;
    }

    /// The advisory preview line under the paste area.
    ftxui::Element preview_line(std::size_t w) const
    {
        using namespace ftxui;
        if (m_import_.source() == ImportSource::file) {
            return text("source: file path (the server reads it)")
                | to_decorator(m_theme_.hint);
        }
        if (m_preview_.lines == 0) {
            return text("(paste full_url,slug pairs, one per line)")
                | to_decorator(m_theme_.hint);
        }
        std::string note = std::to_string(m_preview_.lines)
            + (m_preview_.lines == 1 ? " line" : " lines");
        if (m_preview_.blank_slugs > 0) {
            note += ", " + std::to_string(m_preview_.blank_slugs)
                + (m_preview_.blank_slugs == 1 ? " blank slug" : " blank slugs")
                + " (will be generated)";
        }
        if (!m_preview_.warnings.empty()) {
            note += "; " + std::to_string(m_preview_.warnings.size())
                + (m_preview_.warnings.size() == 1 ? " warning" : " warnings");
        }
        return text(fit(note, w)) | to_decorator(m_theme_.hint);
    }

    /// The dry-run checkbox and the Start / Cancel buttons.
    ftxui::Element action_line(bool start_disabled) const
    {
        using namespace ftxui;
        Element start = text("[ Start import ]");
        if (start_disabled) {
            start = start | to_decorator(m_theme_.disabled);
        }
        return hbox({
            text(m_import_.dry_run() ? "[x] dry run (validate only)"
                                     : "[ ] dry run (validate only)"),
            text("      "),
            std::move(start),
            text("  "),
            text("[ Cancel ]"),
        });
    }

    /// Renders the Export sub-tab.
    ftxui::Element render_export()
    {
        using namespace ftxui;
        const bool path_disabled = writes_denied();

        Elements rows;
        rows.push_back(tab_line());
        rows.push_back(text(""));
        rows.push_back(radio2("Format:  ",
                              m_export_.format() == ImportFormat::csv,
                              "CSV  full_url,slug",
                              "NDJSON"));
        rows.push_back(status_line());
        rows.push_back(text(""));
        rows.push_back(text_field("Query (q):  ", m_q_, "(all)", false, ""));
        rows.push_back(text_field("Tag:        ", m_tag_, "(any)", false, ""));
        rows.push_back(
            text_field("Campaign:   ", m_campaign_, "(any)", false, ""));
        rows.push_back(text_field(
            "File path:  ",
            m_exp_path_,
            "(inline; blank = return data)",
            path_disabled,
            "disabled (read-only: path export needs write, not_authorized)"));
        rows.push_back(text(""));
        rows.push_back(text("[ Start export ]"));
        if (!m_inline_message_.empty()) {
            rows.push_back(text(fit("! " + m_inline_message_, inner_width()))
                           | to_decorator(m_theme_.error));
        }
        rows.push_back(text(""));
        rows.push_back(text("Tab move  Space/</> set  Ctrl-S start  F2 import")
                       | to_decorator(m_theme_.hint));

        const std::string title =
            " Export pairs (command: link.export, runs as a job) ";
        return window(text(title) | to_decorator(m_theme_.title),
                      vbox(std::move(rows)));
    }

    /// The export status radio line (fixed labels only, review 6).
    ftxui::Element status_line() const
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
        cells.push_back(text("Status:  "));
        bool first = true;
        bool any_disabled = false;
        for (const Item& it : items) {
            if (!first) {
                cells.push_back(text("   "));
            }
            first = false;
            const bool sel = it.status == m_export_status_;
            Element cell =
                text((sel ? "(*) " : "( ) ") + std::string(it.label));
            // Review A (display): all/deleted raise export to a write, so a
            // read-only session renders them disabled, not hidden.
            if (!status_queryable(it.status)) {
                cell = cell | to_decorator(m_theme_.disabled);
                any_disabled = true;
            }
            cells.push_back(std::move(cell));
        }
        if (any_disabled) {
            cells.push_back(text("   (all/deleted: not_authorized, read-only)")
                            | to_decorator(m_theme_.disabled));
        }
        return hbox(std::move(cells));
    }

    ///< Alive guard shared with every async completion; cleared by this
    ///< object's destruction so a late task never touches a dead screen
    ///< (T-120).
    std::shared_ptr<bool> m_alive_;

    ///< Screen context (caller, session, router, options); not owned.
    ScreenContext& m_context_;

    ///< Command catalog used to resolve `link.import` / `link.export`.
    std::vector<CommandInfo> m_catalog_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< Wall clock backing the toast queue expiry.
    SystemClock m_clock_;

    ///< Toasts pushed by the submit flow; rendered over the body.
    ToastQueue m_toasts_;

    ///< Confirm policy for the import write (honors `--yes`).
    ConfirmPolicy m_policy_;

    ///< The confirm/submit choke point shared by both sub-tabs.
    MutationFlow m_flow_;

    ///< Source of correlation ids for the submitted requests.
    RequestIdGenerator m_ids_;

    ///< The import form model (owns the import request builder).
    ImportModel m_import_;

    ///< The export form model (owns the export request builder).
    ExportModel m_export_;

    ///< Live import paste buffer (edited in place; synced into @ref m_import_).
    std::string m_paste_;

    ///< Live import file-path buffer.
    std::string m_imp_path_;

    ///< Live export free-text query buffer.
    std::string m_q_;

    ///< Live export tag-filter buffer.
    std::string m_tag_;

    ///< Live export campaign-filter buffer.
    std::string m_campaign_;

    ///< Live export file-path buffer.
    std::string m_exp_path_;

    ///< Export status radio choice; default `all`.
    StatusFilter m_export_status_ = StatusFilter::all;

    ///< Cached advisory preview of @ref m_paste_, refreshed on edit.
    ImportModel::Preview m_preview_;

    ///< Transient blocking message shown after a refused submit; cleared on any
    ///< edit, mode switch or successful submit.
    std::string m_inline_message_;

    ///< Current sub-tab.
    Mode m_mode_ = Mode::import_tab;

    ///< Focus index into @ref controls for the active sub-tab.
    std::size_t m_focus_ = 0;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;
};

/**
 * @brief The Import / Export screen (D7/D11 node): wraps
 * @ref ImportExportComponent as an @ref IScreen and forwards the active-screen
 * lifecycle to it.
 */
class ImportExportScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context and @p catalog.
     * @param context The screen context; captured by the component by
     * reference.
     * @param catalog The command catalog; moved in.
     */
    ImportExportScreen(ScreenContext& context, std::vector<CommandInfo> catalog)
        : m_impl_(
              ftxui::Make<ImportExportComponent>(context, std::move(catalog)))
    {
    }

    ScreenId id() const override { return ScreenId::import_export; }

    ftxui::Component component() override { return m_impl_; }

    std::vector<KeyHint> hints() const override
    {
        return {KeyHint {"Ctrl-S", "start"},
                KeyHint {"F2", "import/export"},
                KeyHint {"Tab", "move"},
                KeyHint {"Esc", "back"}};
    }

    void on_enter() override
    {
        static_cast<ImportExportComponent*>(m_impl_.get())->on_enter();
    }

    void on_leave() override
    {
        static_cast<ImportExportComponent*>(m_impl_.get())->on_leave();
    }

private:
    ///< The Import / Export body component.
    ftxui::Component m_impl_;
};

}  // namespace

std::unique_ptr<IScreen> make_import_export_screen(
    ScreenContext& context, std::vector<CommandInfo> commands)
{
    return std::make_unique<ImportExportScreen>(context, std::move(commands));
}

}  // namespace url_shortener::tui

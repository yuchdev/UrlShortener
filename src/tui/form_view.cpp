/**
 * @file form_view.cpp
 * @brief The D5 form body (subtask 06.0/08): @ref
 * url_shortener::tui::make_form_component frames the per-field widgets, the
 * `[ <verb> ] [ Cancel ]` buttons, the live validation line and the sanitized
 * `Equivalent:` footer, and wires the confirm/submit flow through @ref
 * url_shortener::tui::run_mutation. It builds no JSON (only @ref FormModel
 * does, contracts C1/C8) and is a **T-110 render sink**: every display string -
 * labels, hints, enum values, field echoes, the validation summary, server
 * error text and the footer - is passed through `sanitize_for_terminal` before
 * `text()`.
 */
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/form_view.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <url_shortener/tui/confirm_view.hpp>
#include <url_shortener/tui/equivalent_cmd.hpp>
#include <url_shortener/tui/mutation_flow.hpp>
#include <url_shortener/tui/text_fit.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>

namespace url_shortener::tui
{

namespace
{

/// The 80x24 minimum the frame guarantees; the width guess before @ref reflect
/// measures the real box.
constexpr int k_default_width = 80;

/// Upper bound on the label column width (D5 step 1).
constexpr std::size_t k_label_cap = 14;

/// Placeholder request id used only to render the id-independent `Equivalent:`
/// footer and the `y` copy-hint; @ref equivalent_cli ignores the id, so the
/// footer stays byte-identical to the request actually submitted.
constexpr const char* k_preview_id = "tui-preview";

/// Sanitize (T-110) then truncate @p raw to at most @p width columns.
std::string fit(const std::string& raw, std::size_t width)
{
    return truncate_to_width(sanitize_for_terminal(raw), width);
}

/// Whether @p kind is edited as free text (focus on it is "text input active").
bool is_text_kind(ArgKind kind)
{
    switch (kind) {
        case ArgKind::string:
        case ArgKind::integer:
        case ArgKind::timestamp:
        case ArgKind::string_list:
        case ArgKind::string_map:
        case ArgKind::object:  // raw-JSON object is also typed
            return true;
        case ArgKind::boolean:
        case ArgKind::enumeration:
            return false;
    }
    return true;
}

/// `true` when @p s ends with @p suffix.
bool ends_with(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// The submit-button verb for a command (D5 step 3): `Create` / `Save` / `Run`.
std::string verb_for(const std::string& command)
{
    if (ends_with(command, ".create")) {
        return "Create";
    }
    if (ends_with(command, ".update")) {
        return "Save";
    }
    return "Run";
}

/// The dim hint shown in a field's hint column, by kind (the field's `doc` is
/// used for a plain string/integer when present).
std::string hint_for(const ArgInfo& spec)
{
    switch (spec.kind) {
        case ArgKind::string_list:
            return "comma separated";
        case ArgKind::string_map:
            return "key=value, comma separated";
        case ArgKind::timestamp:
            return "ISO-8601, blank = never";
        case ArgKind::boolean:
        case ArgKind::enumeration:
            return std::string();
        default:
            return spec.doc;  // sanitized at the sink
    }
}

/**
 * @brief A focusable, plain-text action button (`[ Create ]` / `[ Cancel ]`).
 *
 * Rendered without an FTXUI `Button` box so the mono theme and the goldens stay
 * simple; it is dim when disabled and inverted when focused, so its state shows
 * without colour. Enter activates it only when enabled.
 */
class ActionButton final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds a button.
     * @param label Returns the current label text (verb may change with state).
     * @param enabled Returns whether activation is currently allowed.
     * @param on_activate Invoked on Enter when enabled.
     * @param theme Theme for the disabled style; held by reference, must
     * outlive this.
     */
    ActionButton(std::function<std::string()> label,
                 std::function<bool()> enabled,
                 std::function<void()> on_activate,
                 const Theme& theme)
        : m_label_(std::move(label))
        , m_enabled_(std::move(enabled))
        , m_on_activate_(std::move(on_activate))
        , m_theme_(theme)
    {
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        Element e = text("[ " + sanitize_for_terminal(m_label_()) + " ]");
        if (!m_enabled_()) {
            e = e | to_decorator(m_theme_.disabled);
        }
        if (Focused()) {
            e = e | inverted;
        }
        return e;
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        if (event == ftxui::Event::Return) {
            if (m_enabled_()) {
                m_on_activate_();
            }
            return true;
        }
        return false;
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

private:
    std::function<std::string()> m_label_;  ///< Current label provider.
    std::function<bool()> m_enabled_;  ///< Current enabled provider.
    std::function<void()> m_on_activate_;  ///< Enter action.
    const Theme& m_theme_;  ///< Disabled style; not owned.
};

/**
 * @brief A focusable `[x] label` / `[ ] label` checkbox toggled with Space.
 *
 * Used for the form's dry-run toggle (offered only when the command supports
 * `options.dry_run`, D5 step / FormOptions::include_dry_run_toggle). Inverted
 * when focused so its state reads without colour (mono-safe); the label is
 * composition text, sanitized at the sink for defence in depth.
 */
class CheckToggle final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the toggle.
     * @param label The fixed control label (e.g. `"dry-run"`).
     * @param is_on Returns the current on/off state.
     * @param on_toggle Flips the state (Space).
     */
    CheckToggle(std::string label,
                std::function<bool()> is_on,
                std::function<void()> on_toggle)
        : m_label_(std::move(label))
        , m_is_on_(std::move(is_on))
        , m_on_toggle_(std::move(on_toggle))
    {
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        Element e = text((m_is_on_() ? "[x] " : "[ ] ")
                         + sanitize_for_terminal(m_label_));
        return Focused() ? (e | inverted) : e;
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        if (event == ftxui::Event::Character(" ")) {
            m_on_toggle_();
            return true;
        }
        return false;
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

private:
    std::string m_label_;  ///< Fixed control label.
    std::function<bool()> m_is_on_;  ///< Current state provider.
    std::function<void()> m_on_toggle_;  ///< Space action.
};

/**
 * @brief The complete D5 form: fields, buttons, validation line, footer and the
 * confirm/submit flow (subtask 06.0/08).
 *
 * Owns the per-field widgets inside a vertical container (native Tab/arrow
 * focus), an embedded @ref MutationFlow for the confirm/submit choke point, and
 * a discard-prompt dialog. Rendering is a pure function of @ref FormModel state
 * plus focus, so goldens are reproducible.
 */
class FormComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the form over @p model and @p ctx.
     * @param model The form to edit; held by reference, must outlive this.
     * @param ctx The owning screen's collaborators; held by reference, must
     * outlive this.
     */
    FormComponent(FormModel& model, FormViewContext& ctx)
        : m_model_(model)
        , m_ctx_(ctx)
        , m_flow_(MutationDeps {
              ctx.caller, ctx.policy, ctx.toasts, ctx.theme, ctx.read_only})
    {
        using namespace ftxui;

        Components children;
        for (std::size_t i = 0; i < m_model_.fields().size(); ++i) {
            Component w = make_field_widget(m_model_, i, m_ctx_.theme);
            m_field_widgets_.push_back(w);
            children.push_back(w);
        }

        // Offer the dry-run toggle only when the command honors it (D5 /
        // FormOptions::include_dry_run_toggle), between the fields and buttons.
        if (m_model_.dry_run_available()) {
            m_dry_run_ = Make<CheckToggle>(
                "dry-run",
                [this] { return m_model_.dry_run(); },
                [this] { m_model_.set_dry_run(!m_model_.dry_run()); });
            children.push_back(m_dry_run_);
        }

        m_create_ = Make<ActionButton>(
            [this] { return verb_for(m_model_.command().name); },
            [this] { return submit_enabled(); },
            [this] { try_submit(); },
            m_ctx_.theme);
        m_cancel_ = Make<ActionButton>([] { return std::string("Cancel"); },
                                       [] { return true; },
                                       [this] { request_cancel(); },
                                       m_ctx_.theme);
        m_buttons_ = Container::Horizontal({m_create_, m_cancel_});
        children.push_back(m_buttons_);

        m_root_ = Container::Vertical(children);
        Add(m_root_);
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        Element body = render_form();
        if (m_flow_.awaiting_confirm()) {
            body = dbox({std::move(body), m_flow_.render_overlay()});
        }
        else if (m_discarding_ && m_discard_dialog_) {
            body = dbox({std::move(body), m_discard_dialog_->Render()});
        }
        if (m_ctx_.theme.ascii_only) {
            body = ascii_filter(std::move(body));
        }
        return body | reflect(m_box_);
    }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        using namespace ftxui;

        // Modal overlays win over the form while shown.
        if (m_flow_.awaiting_confirm()) {
            const bool r = m_flow_.overlay_event(event);
            update_text_active();
            return r;
        }
        if (m_discarding_) {
            route_discard(event);
            return true;
        }

        if (event == Event::CtrlS) {
            try_submit();
            update_text_active();
            return true;
        }
        if (event == Event::Escape) {
            request_cancel();
            update_text_active();
            return true;
        }

        const bool consumed = m_root_->OnEvent(event);
        update_text_active();
        if (consumed) {
            return true;
        }
        if (event == Event::Return) {
            m_model_.validate();  // refresh the summary on Enter (blur)
            return true;
        }
        if (event == Event::Character("y")) {
            copy_hint();  // only reached when no text field ate the 'y'
            return true;
        }
        return false;
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

private:
    /// Whether the submit button is currently active.
    bool submit_enabled() const
    {
        if (m_ctx_.read_only && !is_read_only_command(m_model_.command())) {
            return false;
        }
        return !m_flow_.busy() && !m_flow_.awaiting_confirm();
    }

    /// Validates and, if clean, builds the one request and routes it through
    /// the mutation choke point (every mutating submit confirms; a read submits
    /// directly).
    void try_submit()
    {
        if (!submit_enabled()) {
            return;
        }
        if (!m_model_.validate()) {
            return;  // inline errors + summary now show the problem
        }
        commands::CommandRequest request = m_model_.to_request(m_ids_.next());
        run_mutation(m_flow_,
                     std::move(request),
                     m_model_.command(),
                     [this](const commands::CommandResponse& resp)
                     { on_result(resp); });
    }

    /// Handles a completed submit: navigate on success, inline-annotate a named
    /// field error on failure (the toast is pushed by the flow).
    void on_result(const commands::CommandResponse& resp)
    {
        if (resp.ok) {
            if (m_ctx_.on_success) {
                m_ctx_.on_success(resp);
            }
            return;
        }
        assign_inline_error(resp.error);
    }

    /// Sets @ref FormField::error on any field whose name the server error
    /// message mentions (the server names the field when it can; otherwise the
    /// toast alone carries the failure).
    void assign_inline_error(const commands::CommandError& error)
    {
        if (error.message.empty()) {
            return;
        }
        for (FormField& f : m_model_.fields()) {
            if (!f.spec.name.empty()
                && error.message.find(f.spec.name) != std::string::npos)
            {
                f.error = error.message;  // raw; sanitized at the sink
            }
        }
    }

    /// Cancel request (Cancel button or Esc): cancel immediately when nothing
    /// is touched, otherwise open the discard prompt.
    void request_cancel()
    {
        bool touched = false;
        for (const FormField& f : m_model_.fields()) {
            if (f.state != FieldState::untouched) {
                touched = true;
                break;
            }
        }
        if (!touched) {
            if (m_ctx_.on_cancel) {
                m_ctx_.on_cancel();
            }
            return;
        }
        ConfirmRequest req;
        req.kind = ConfirmKind::yes_no;
        req.title = "Discard changes";
        req.prompt = "Discard unsaved changes?";
        m_discard_ = std::make_unique<ConfirmModel>(std::move(req));
        m_discard_dialog_ = confirm_dialog(*m_discard_, m_ctx_.theme);
        m_discarding_ = true;
    }

    /// Routes an event to the open discard prompt and acts on its decision.
    void route_discard(const ftxui::Event& event)
    {
        m_discard_dialog_->OnEvent(event);
        const std::optional<bool> decided = m_discard_->result();
        if (decided.has_value()) {
            m_discarding_ = false;
            const bool discard = *decided;
            m_discard_.reset();
            m_discard_dialog_.reset();
            if (discard && m_ctx_.on_cancel) {
                m_ctx_.on_cancel();
            }
        }
    }

    /// Shows the full `Equivalent:` CLI line in an info toast (`y`); clipboard
    /// is out of scope (T-110/T-125).
    void copy_hint()
    {
        Toast t;
        t.kind = ToastKind::info;
        t.title = "equivalent command";
        t.body = equivalent_cli(m_model_.to_request(k_preview_id));
        m_ctx_.toasts.push(std::move(t));
    }

    /// Mirrors text-field focus into the screen via @ref
    /// FormViewContext::on_text_input_changed, firing only on a change.
    void update_text_active()
    {
        bool active = false;
        for (std::size_t i = 0; i < m_field_widgets_.size(); ++i) {
            if (m_field_widgets_[i]->Focused() && i < m_model_.fields().size())
            {
                active = is_text_kind(m_model_.fields()[i].spec.kind);
                break;
            }
        }
        if (active != m_last_text_active_) {
            m_last_text_active_ = active;
            if (m_ctx_.on_text_input_changed) {
                m_ctx_.on_text_input_changed(active);
            }
        }
    }

    /// The current body width from the previous frame, or the 80-column guess.
    int measured_width() const
    {
        const int w = m_box_.x_max - m_box_.x_min + 1;
        return w > 1 ? w : k_default_width;
    }

    /// The label column width: the longest field name, capped at @ref
    /// k_label_cap, plus one column for the required `*` marker.
    std::size_t label_width() const
    {
        std::size_t longest = 0;
        for (const FormField& f : m_model_.fields()) {
            longest = std::max(longest, display_width(f.spec.name));
        }
        return std::min(longest, k_label_cap) + 1;
    }

    /// Builds one field row: `label*  <widget>  hint` (or the inline error in
    /// the hint column, in error style), focus-wrapped so `yframe` keeps it
    /// visible.
    ftxui::Element field_row(std::size_t i, std::size_t label_cols) const
    {
        using namespace ftxui;
        const FormField& f = m_model_.fields()[i];
        const std::string marker = f.spec.required ? "*" : " ";
        const std::string label =
            pad_to_width(fit(f.spec.name, k_label_cap) + marker, label_cols);

        Element hint_el;
        if (f.error.has_value()) {
            hint_el =
                text(fit(*f.error, 34)) | to_decorator(m_ctx_.theme.error);
        }
        else {
            // A nullable field shows a visible `[x] clear` / `[ ] clear` box
            // (bound to FormModel::clear_field, toggled with Delete) ahead of
            // its hint, in the fixed hint column so it is never clipped.
            std::string hint = hint_for(f.spec);
            if (f.spec.nullable) {
                const std::string box =
                    f.state == FieldState::cleared ? "[x] clear" : "[ ] clear";
                hint = hint.empty() ? box : box + "  " + hint;
            }
            hint_el = text(fit(hint, 34)) | to_decorator(m_ctx_.theme.hint);
        }

        Element row = hbox({
            text(label) | size(WIDTH, EQUAL, static_cast<int>(label_cols)),
            m_field_widgets_[i]->Render() | flex,
            text("  "),
            std::move(hint_el),
        });
        if (m_field_widgets_[i]->Focused()) {
            row = row | focus;  // keep the focused row inside the yframe
        }
        return row;
    }

    /// Builds the windowed D5 body.
    ftxui::Element render_form()
    {
        using namespace ftxui;
        const int width = measured_width();
        const std::size_t label_cols = label_width();

        Elements rows;
        for (std::size_t i = 0; i < m_model_.fields().size(); ++i) {
            rows.push_back(field_row(i, label_cols));
        }
        Element fields = yframe(vbox(std::move(rows)));

        const std::string summary = m_model_.validation_summary();
        Elements button_cells {
            m_create_->Render(),
            text("  "),
            m_cancel_->Render(),
        };
        if (m_dry_run_) {
            button_cells.push_back(text("   "));
            button_cells.push_back(m_dry_run_->Render());
        }
        button_cells.push_back(filler());
        button_cells.push_back(text("validation: " + fit(summary, 36))
                               | to_decorator(m_ctx_.theme.hint));
        Element buttons = hbox(std::move(button_cells));

        const std::size_t footer_budget =
            width > 14 ? static_cast<std::size_t>(width - 14) : 0u;
        const std::string cli =
            equivalent_cli(m_model_.to_request(k_preview_id));
        Element footer = hbox({
            text("Equivalent: ") | to_decorator(m_ctx_.theme.hint),
            text(fit(cli, footer_budget)),
        });

        Elements content_rows {
            std::move(fields) | flex,
            text(""),
            std::move(buttons),
        };
        // ADR 0003 rule 5 / subtask 18: in a read-only session a write
        // command's disabled Create button carries the registry's
        // not_authorized reason, so the control is disabled *and labelled*,
        // never silently inert.
        if (m_ctx_.read_only && !is_read_only_command(m_model_.command())) {
            content_rows.push_back(
                text("read-only session: " + m_model_.command().name
                     + " needs write access (not_authorized)")
                | to_decorator(m_ctx_.theme.disabled));
        }
        content_rows.push_back(separator());
        content_rows.push_back(std::move(footer));
        Element content = vbox(std::move(content_rows));

        const std::string title_text =
            (m_ctx_.title.empty() ? m_model_.command().name : m_ctx_.title)
            + "  (command: " + m_model_.command().name + ")";
        Element title =
            text(" "
                 + fit(title_text,
                       static_cast<std::size_t>(width > 4 ? width - 4 : 0))
                 + " ")
            | to_decorator(m_ctx_.theme.title);
        return window(std::move(title), std::move(content));
    }

    ///< The form being edited; not owned, outlives this component.
    FormModel& m_model_;

    ///< The owning screen's collaborators; not owned, outlives this component.
    FormViewContext& m_ctx_;

    ///< Source of correlation ids for the submitted request.
    RequestIdGenerator m_ids_;

    ///< The confirm/submit choke point for this form.
    MutationFlow m_flow_;

    ///< One widget per field, parallel to @ref FormModel::fields; also held by
    ///< @ref m_root_ for focus/event routing.
    std::vector<ftxui::Component> m_field_widgets_;

    ///< The submit button.
    ftxui::Component m_create_;

    ///< The cancel button.
    ftxui::Component m_cancel_;

    ///< The dry-run toggle, or null when the command does not support dry-run.
    ftxui::Component m_dry_run_;

    ///< The horizontal button row (create + cancel).
    ftxui::Component m_buttons_;

    ///< The vertical container owning the fields and the button row (native
    ///< Tab/arrow focus).
    ftxui::Component m_root_;

    ///< The open discard-prompt state; null when no prompt is shown.
    std::unique_ptr<ConfirmModel> m_discard_;

    ///< The discard-prompt dialog bound to @ref m_discard_.
    ftxui::Component m_discard_dialog_;

    ///< `true` while the discard prompt is open.
    bool m_discarding_ = false;

    ///< Last reported text-input-active state, to fire the callback on change.
    bool m_last_text_active_ = false;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;
};

}  // namespace

ftxui::Component make_form_component(FormModel& model, FormViewContext& ctx)
{
    return ftxui::Make<FormComponent>(model, ctx);
}

}  // namespace url_shortener::tui

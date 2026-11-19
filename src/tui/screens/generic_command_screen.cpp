/**
 * @file generic_command_screen.cpp
 * @brief The generic command screen (D9/D11, subtask 06.0/11): generated form
 * (or raw-JSON args), confirm choke point, and a scrollable result viewer for
 * one command picked from the Command Palette. Every mutating submit - form,
 * raw-JSON (`Ctrl-R`), and no-arg mutating command - routes through
 * @ref url_shortener::tui::run_mutation with the session read-only flag (ADR
 * 0003 rule 4, threat model T-116); every displayed string is sanitized
 * (T-110); the only JSON built is through the sanctioned scanner
 * (`json_view::reserialize_object`), never a local tokenizer (contract C9).
 */
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/screens/generic_command_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/equivalent_cmd.hpp>
#include <url_shortener/tui/form_model.hpp>
#include <url_shortener/tui/form_view.hpp>
#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/keymap.hpp>
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

/// Request id used only to render the result viewer's `y` copy-command toast;
/// `equivalent_cli` ignores the id, so it matches the submitted request.
constexpr const char* k_preview_id = "tui-preview";

/// Sanitize (T-110) then truncate @p raw to at most @p width columns.
std::string fit(const std::string& raw, std::size_t width)
{
    return truncate_to_width(sanitize_for_terminal(raw), width);
}

/**
 * @brief The generic command body: generated/raw-JSON form, confirm, result
 * viewer (subtask 06.0/11).
 */
class GenericCommandComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the component over @p context and @p config.
     * @param context The screen context; its caller/router are references owned
     * by @ref App that must outlive this component (captured by reference).
     * @param config The command and close hook; moved in.
     */
    GenericCommandComponent(ScreenContext& context, GenericCommandConfig config)
        : m_context_(context)
        , m_toasts_(m_clock_)
        , m_policy_(context.options.assume_yes)
        , m_theme_(theme_for(context.options.theme))
        , m_on_close_(std::move(config.on_close))
        , m_model_(config.command)
        , m_raw_flow_(MutationDeps {context.caller,
                                    m_policy_,
                                    m_toasts_,
                                    m_theme_,
                                    capability_for(context)})
    {
        FormViewContext view_ctx {
            m_context_.caller,
            m_policy_,
            m_toasts_,
            m_theme_,
            capability_for(m_context_),
            m_model_.command().name,
            [this](const commands::CommandResponse& resp)
            { on_submit_result(resp, m_model_.to_request(k_preview_id)); },
            [this] { close(); },
            [this](bool active) { m_context_.text_input_active = active; },
        };
        m_view_ctx_ = std::make_unique<FormViewContext>(std::move(view_ctx));
        m_form_ = make_form_component(m_model_, *m_view_ctx_);
        Add(m_form_);
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override
    {
        using namespace ftxui;
        Element body;
        if (m_mode_ == Mode::result) {
            body = render_result();
        }
        else if (m_raw_mode_) {
            body = render_raw();
            if (m_raw_flow_.awaiting_confirm()) {
                body = dbox({std::move(body), m_raw_flow_.render_overlay()});
            }
        }
        else {
            body = m_form_->Render();
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
        if (m_mode_ == Mode::result) {
            return result_keys(event);
        }
        // The raw-JSON confirm dialog is modal over the editor.
        if (m_raw_mode_ && m_raw_flow_.awaiting_confirm()) {
            return m_raw_flow_.overlay_event(event);
        }
        // Raw-JSON toggle. The spec named Ctrl-J, but FTXUI maps Ctrl-J to the
        // same byte (0x0A) as Enter/Return, so hijacking it would break confirm
        // and button activation; Ctrl-R is used instead (see subtask
        // deviation).
        if (event == Event::CtrlR) {
            toggle_raw();
            return true;
        }
        if (m_raw_mode_) {
            if (event == Event::CtrlS) {
                submit_raw();
                return true;
            }
            if (event == Event::Escape) {
                close();
                return true;
            }
            if (event == Event::Backspace) {
                raw_backspace();
                return true;
            }
            if (event == Event::Return) {
                raw_append("\n");
                return true;
            }
            if (event.is_character()) {
                raw_append(event.character());
                return true;
            }
            return true;  // the raw editor owns the keyboard while shown
        }
        // Generated-form mode: a job just accepted lets `5` jump to Jobs.
        if (event == Event::Character("5") && m_job_pending_
            && !m_context_.text_input_active)
        {
            m_context_.router.navigate_tab('5');
            m_job_pending_ = false;
            return true;
        }
        return m_form_->OnEvent(event);
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return true; }

private:
    ///< The two screen states: entering arguments vs viewing a result.
    enum class Mode
    {
        form,
        result
    };

    /// Handles a successful submit (form or raw path): a job leaves the form
    /// open (the flow toasts "job accepted"); any other result opens the
    /// viewer.
    void on_submit_result(const commands::CommandResponse& resp,
                          const commands::CommandRequest& request)
    {
        if (!resp.ok) {
            return;  // the flow already toasted the error; form stays open
        }
        m_last_request_ = request;
        if (resp.job.has_value()) {
            m_job_pending_ = true;
            return;
        }
        m_result_ =
            std::make_unique<ResultViewerModel>(resp.result_json, view_width());
        m_mode_ = Mode::result;
    }

    /// Submits the raw-JSON args after validating they are exactly one
    /// well-formed object; routes through the confirm/read-only choke point.
    void submit_raw()
    {
        if (m_raw_flow_.busy()) {
            return;
        }
        const std::optional<std::string> args =
            reserialize_object(m_raw_buffer_);
        if (!args.has_value()) {
            Toast t;
            t.kind = ToastKind::error;
            t.title = "! invalid_args";
            t.body = "raw args must be exactly one JSON object";
            t.source_command = m_model_.command().name;
            m_toasts_.push(std::move(t));
            return;
        }
        commands::CommandRequest request;
        request.v = 1;
        request.id = m_ids_.next();
        request.command = m_model_.command().name;
        request.args_json = *args;
        m_last_request_ = request;
        run_mutation(m_raw_flow_,
                     commands::CommandRequest(request),
                     m_model_.command(),
                     [this](const commands::CommandResponse& resp)
                     { on_submit_result(resp, m_last_request_); });
    }

    /// Toggles the raw-JSON args editor, seeding it with the form's current
    /// args object (`{}` when untouched).
    void toggle_raw()
    {
        m_raw_mode_ = !m_raw_mode_;
        if (m_raw_mode_) {
            m_raw_buffer_ = m_model_.to_args_json();
            m_context_.text_input_active = true;
        }
        else {
            m_context_.text_input_active = false;
        }
    }

    /// Appends @p s to the raw buffer, capped at the field byte bound (T-121).
    void raw_append(const std::string& s)
    {
        if (m_raw_buffer_.size() + s.size() <= FormModel::k_max_field_bytes) {
            m_raw_buffer_ += s;
        }
    }

    /// Deletes the last UTF-8 code point from the raw buffer.
    void raw_backspace()
    {
        if (m_raw_buffer_.empty()) {
            return;
        }
        std::size_t cut = m_raw_buffer_.size() - 1;
        while (cut > 0
               && (static_cast<unsigned char>(m_raw_buffer_[cut]) & 0xC0u)
                   == 0x80u)
        {
            --cut;
        }
        m_raw_buffer_.erase(cut);
    }

    /// Result-viewer keys: scroll, `y` copy-command, `Esc` back.
    bool result_keys(const ftxui::Event& event)
    {
        using namespace ftxui;
        if (event == Event::Escape) {
            close();
            return true;
        }
        if (event == Event::Character("y")) {
            Toast t;
            t.kind = ToastKind::info;
            t.title = "equivalent command";
            t.body = equivalent_cli(m_last_request_);
            m_toasts_.push(std::move(t));
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

    /// Returns to the caller (the Palette list).
    void close()
    {
        if (m_on_close_) {
            m_on_close_();
        }
    }

    /// The inner content width from the last frame, or the 80-column guess.
    int measured_width() const
    {
        const int w = m_box_.x_max - m_box_.x_min + 1;
        return w > 1 ? w : k_default_width;
    }

    /// Wrapping width handed to the result viewer (inside the window border).
    std::size_t view_width() const
    {
        const int w = measured_width() - 4;
        return static_cast<std::size_t>(w > 8 ? w : 72);
    }

    /// Rows of result text visible inside the window.
    std::size_t view_height() const
    {
        const int h = m_box_.y_max - m_box_.y_min + 1;
        const int rows = h - 4;
        return static_cast<std::size_t>(rows > 1 ? rows : 18);
    }

    /// The result-viewer body (D9 step 5): a titled, scrollable JSON view.
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
            lines.push_back(text("(empty result)")
                            | to_decorator(m_theme_.hint));
        }
        std::string footer = "j/k scroll  y copy cmd  Esc back";
        if (m_result_->truncated()) {
            footer += "   (result truncated)";
        }
        Element content = vbox({
            vbox(std::move(lines)) | flex,
            separator(),
            text(footer) | to_decorator(m_theme_.hint),
        });
        const std::string title =
            " result: " + fit(m_model_.command().name, 40) + " ";
        return window(text(title) | to_decorator(m_theme_.title),
                      std::move(content));
    }

    /// The raw-JSON args editor body (D9 step 4).
    ftxui::Element render_raw()
    {
        using namespace ftxui;
        const std::size_t width = static_cast<std::size_t>(
            measured_width() > 4 ? measured_width() - 4 : 0);
        Elements buffer_lines;
        std::size_t start = 0;
        while (start <= m_raw_buffer_.size()) {
            const std::size_t nl = m_raw_buffer_.find('\n', start);
            const std::size_t end =
                nl == std::string::npos ? m_raw_buffer_.size() : nl;
            buffer_lines.push_back(
                text(fit(m_raw_buffer_.substr(start, end - start), width)));
            if (nl == std::string::npos) {
                break;
            }
            start = nl + 1;
        }
        Element content = vbox({
            text("Edit args as one JSON object:") | to_decorator(m_theme_.hint),
            text(""),
            vbox(std::move(buffer_lines)) | flex,
            separator(),
            text("Ctrl-S run   Ctrl-R form   Esc cancel")
                | to_decorator(m_theme_.hint),
        });
        const std::string title =
            " " + fit(m_model_.command().name, 40) + "  (raw JSON args) ";
        return window(text(title) | to_decorator(m_theme_.title),
                      std::move(content));
    }

    ///< Screen context (caller, session, router, options); not owned.
    ScreenContext& m_context_;

    ///< Wall clock backing the toast queue expiry.
    SystemClock m_clock_;

    ///< Toasts pushed by the submit flows; rendered over the body.
    ToastQueue m_toasts_;

    ///< Confirm policy for mutating submits (honors `--yes`).
    ConfirmPolicy m_policy_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< Close hook back to the Palette list; may be empty.
    std::function<void()> m_on_close_;

    ///< The edited form model for the command.
    FormModel m_model_;

    ///< Source of correlation ids for the raw-JSON submit path.
    RequestIdGenerator m_ids_;

    ///< The confirm/submit choke point for the raw-JSON path (the generated
    ///< form has its own internal flow).
    MutationFlow m_raw_flow_;

    ///< The view context handed to the form component; heap-held so its
    ///< reference members stay stable for the component's lifetime.
    std::unique_ptr<FormViewContext> m_view_ctx_;

    ///< The generated D5 form body.
    ftxui::Component m_form_;

    ///< `true` while the raw-JSON args editor replaces the generated form.
    bool m_raw_mode_ = false;

    ///< The raw-JSON args buffer (operator-typed).
    std::string m_raw_buffer_ = "{}";

    ///< Current screen state.
    Mode m_mode_ = Mode::form;

    ///< The result viewer, built on a successful immediate result.
    std::unique_ptr<ResultViewerModel> m_result_;

    ///< The last submitted request, for the result viewer's `y` copy-command.
    commands::CommandRequest m_last_request_;

    ///< `true` after a job response, so `5` can jump to the Jobs screen.
    bool m_job_pending_ = false;

    ///< Body box measured by @ref ftxui::reflect on the previous frame.
    ftxui::Box m_box_;
};

/**
 * @brief The generic command screen (D9/D11 node): wraps
 * @ref GenericCommandComponent as an @ref IScreen.
 */
class GenericCommandScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context and @p config.
     * @param context The screen context; captured by the component by
     * reference.
     * @param config The command and close hook; moved in.
     */
    GenericCommandScreen(ScreenContext& context, GenericCommandConfig config)
        : m_component_(
              ftxui::Make<GenericCommandComponent>(context, std::move(config)))
    {
    }

    ScreenId id() const override { return ScreenId::generic_form; }

    ftxui::Component component() override { return m_component_; }

    std::vector<KeyHint> hints() const override
    {
        return {KeyHint {"Ctrl-S", "run"},
                KeyHint {"Ctrl-R", "raw JSON"},
                KeyHint {"y", "copy cmd"},
                KeyHint {"Esc", "back"}};
    }

private:
    ///< The command body component.
    ftxui::Component m_component_;
};

}  // namespace

std::unique_ptr<IScreen> make_generic_command_screen(
    ScreenContext& context, GenericCommandConfig config)
{
    return std::make_unique<GenericCommandScreen>(context, std::move(config));
}

}  // namespace url_shortener::tui

/**
 * @file form_screen.cpp
 * @brief The generic D5 form screen (subtask 06.0/08): an @ref
 * url_shortener::tui::IScreen hosting a @ref url_shortener::tui::FormModel and
 * its @ref url_shortener::tui::make_form_component body, with an owned confirm
 * policy and toast queue. Submits go off the UI thread through the injected
 * `AsyncCaller`; mutating submits pass the confirm choke point. Every displayed
 * server string is sanitized by the view before `text()` (threat model T-110);
 * the screen itself builds no JSON and calls no `ICommandClient::call` directly
 * (contracts C1/C8).
 */
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/form_screen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/tui/confirm.hpp>
#include <url_shortener/tui/form_model.hpp>
#include <url_shortener/tui/form_view.hpp>
#include <url_shortener/tui/keymap.hpp>
#include <url_shortener/tui/theme.hpp>
#include <url_shortener/tui/theme_ftxui.hpp>
#include <url_shortener/tui/toast.hpp>
#include <url_shortener/tui/toast_view.hpp>

namespace url_shortener::tui
{

namespace
{

/**
 * @brief The generic D5 form screen: a form body plus its toast overlay.
 *
 * Owns the @ref FormModel, the @ref ConfirmPolicy and the @ref ToastQueue the
 * form submits through, and layers @ref render_toasts over the form body. Holds
 * the @ref ScreenContext by reference for the caller, session and options, and
 * mirrors text-field focus into @ref ScreenContext::text_input_active.
 */
class FormScreen final : public IScreen
{
public:
    /**
     * @brief Builds the screen over @p context and @p config.
     *
     * @param context The screen context; its `caller` and router are references
     * owned by @ref App that must outlive this screen (captured by reference).
     * @param config The command, id, title, optional prefill and hooks; moved
     * in.
     */
    FormScreen(ScreenContext& context, FormScreenConfig config)
        : m_context_(context)
        , m_id_(config.id)
        , m_toasts_(m_clock_)
        , m_policy_(context.options.assume_yes)
        , m_model_(config.command)
        , m_theme_(theme_for(context.options.theme))
        , m_on_success_(std::move(config.on_success))
        , m_on_cancel_(std::move(config.on_cancel))
        , m_title_(std::move(config.title))
    {
        if (!config.prefill_result.empty()) {
            m_model_.prefill_from_result(config.prefill_result);
        }

        FormViewContext view_ctx {
            m_context_.caller,
            m_policy_,
            m_toasts_,
            m_theme_,
            m_context_.options.read_only,
            m_title_,
            [this](const commands::CommandResponse& resp)
            {
                if (m_on_success_) {
                    m_on_success_(resp);
                }
            },
            [this]
            {
                if (m_on_cancel_) {
                    m_on_cancel_();
                }
            },
            [this](bool active) { m_context_.text_input_active = active; },
        };
        m_view_ctx_ = std::make_unique<FormViewContext>(std::move(view_ctx));
        m_form_ = make_form_component(m_model_, *m_view_ctx_);

        // Renderer draws the form plus its toast overlay and forwards events to
        // the form (so Ctrl-S / Esc / typing all reach it).
        m_component_ = ftxui::Renderer(
            m_form_,
            [this]
            {
                return ftxui::dbox(
                    {m_form_->Render(),
                     render_toasts(
                         m_toasts_, /*show_details=*/false, m_theme_)});
            });
    }

    ScreenId id() const override { return m_id_; }

    ftxui::Component component() override { return m_component_; }

    std::vector<KeyHint> hints() const override
    {
        return {KeyHint {"Ctrl-S", "submit"},
                KeyHint {"Esc", "cancel"},
                KeyHint {"y", "copy cmd"}};
    }

private:
    ///< Screen context (caller, session, options); not owned, outlives this.
    ScreenContext& m_context_;

    ///< The screen id this form reports.
    ScreenId m_id_;

    ///< Wall clock backing the toast queue's expiry.
    SystemClock m_clock_;

    ///< Toasts pushed by the submit flow; rendered over the form body.
    ToastQueue m_toasts_;

    ///< Confirm policy for mutating submits (honors `--yes`).
    ConfirmPolicy m_policy_;

    ///< The edited form model (prefilled for an edit flow).
    FormModel m_model_;

    ///< Active theme; a process-lifetime reference from @ref theme_for.
    const Theme& m_theme_;

    ///< Success hook supplied by the caller; may be empty.
    std::function<void(const commands::CommandResponse&)> m_on_success_;

    ///< Cancel hook supplied by the caller; may be empty.
    std::function<void()> m_on_cancel_;

    ///< Window title (command name appended by the view).
    std::string m_title_;

    ///< The view context handed to the form component; heap-held so its
    ///< reference members stay stable for the component's lifetime.
    std::unique_ptr<FormViewContext> m_view_ctx_;

    ///< The form body component.
    ftxui::Component m_form_;

    ///< The root component (form body + toast overlay).
    ftxui::Component m_component_;
};

}  // namespace

std::unique_ptr<IScreen> make_form_screen(ScreenContext& context,
                                          FormScreenConfig config)
{
    return std::make_unique<FormScreen>(context, std::move(config));
}

}  // namespace url_shortener::tui

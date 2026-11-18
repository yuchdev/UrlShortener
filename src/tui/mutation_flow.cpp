/**
 * @file mutation_flow.cpp
 * @brief The single mutating-submit choke point (subtask 06.0/08): decide the
 * confirmation with @ref url_shortener::tui::ConfirmPolicy, route a required
 * confirm through the D10 dialog, submit off the UI thread with
 * @ref url_shortener::tui::AsyncCaller, and derive a @ref
 * url_shortener::tui::Toast from the response. No business logic and no JSON
 * construction live here (contracts C1/C8); the confirm dialog is the only
 * rendering, and it reuses `confirm_view.cpp`, the T-110 sink.
 */
#include <utility>

#include "url_shortener/tui/mutation_flow.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <url_shortener/tui/confirm_view.hpp>

namespace url_shortener::tui
{

namespace
{

/**
 * @brief The always-present overlay that renders and drives @ref MutationFlow's
 * confirm dialog.
 *
 * Holds only a back-pointer to the flow (which owns the dialog state); it is
 * modal while a confirm is open and inert otherwise, so a screen can layer it
 * over its body unconditionally.
 */
class MutationOverlayComponent final : public ftxui::ComponentBase
{
public:
    /**
     * @brief Builds the overlay over @p flow.
     * @param flow The flow whose confirm state this overlay renders and drives;
     * a non-owning pointer that must outlive the component (the flow owns it).
     */
    explicit MutationOverlayComponent(MutationFlow* flow)
        : m_flow_(flow)
    {
    }

    /// @copydoc ftxui::ComponentBase::OnRender
    ftxui::Element OnRender() override { return m_flow_->render_overlay(); }

    /// @copydoc ftxui::ComponentBase::OnEvent
    bool OnEvent(ftxui::Event event) override
    {
        return m_flow_->overlay_event(event);
    }

    /// @copydoc ftxui::ComponentBase::Focusable
    bool Focusable() const override { return m_flow_->awaiting_confirm(); }

private:
    ///< The flow driving this overlay; not owned, outlives the component.
    MutationFlow* m_flow_;
};

}  // namespace

MutationFlow::MutationFlow(MutationDeps deps)
    : m_deps_(deps)
{
    m_overlay_ = ftxui::Make<MutationOverlayComponent>(this);
}

bool MutationFlow::busy() const
{
    return m_deps_.caller.busy();
}

void MutationFlow::begin(
    commands::CommandRequest request,
    const CommandInfo& info,
    std::function<void(const commands::CommandResponse&)> on_result)
{
    m_request_ = std::move(request);
    m_command_name_ = info.name;
    m_on_result_ = std::move(on_result);

    ConfirmRequest decision = m_deps_.policy.decide(info, m_request_.args_json);
    if (decision.kind == ConfirmKind::none) {
        // No friction (non-mutating command, or a --yes session): submit now.
        m_awaiting_ = false;
        m_confirm_.reset();
        submit_now();
        return;
    }

    // A confirm is required: open the dialog and defer the submit until the
    // operator confirms. Nothing reaches the caller until then (criterion 4).
    m_confirm_ = std::make_unique<ConfirmModel>(std::move(decision));
    m_dialog_ = confirm_dialog(*m_confirm_, m_deps_.theme);
    m_awaiting_ = true;
}

ftxui::Element MutationFlow::render_overlay() const
{
    if (!m_awaiting_ || !m_dialog_) {
        return ftxui::text("");
    }
    return m_dialog_->Render();
}

bool MutationFlow::overlay_event(const ftxui::Event& event)
{
    if (!m_awaiting_ || !m_dialog_) {
        return false;
    }
    m_dialog_->OnEvent(event);
    const std::optional<bool> decided = m_confirm_->result();
    if (decided.has_value()) {
        m_awaiting_ = false;
        if (*decided) {
            submit_now();
        }
        // A cancelled confirm discards the deferred submission (zero calls).
        m_confirm_.reset();
        m_dialog_.reset();
    }
    return true;  // modal while the dialog is shown
}

void MutationFlow::submit_now()
{
    // The handler captures `this`: the flow (owned by the screen) outlives the
    // AsyncCaller (App destroys the caller before its screens), and the caller
    // suppresses completions once destroyed (T-120), so the raw capture cannot
    // run against a dead flow.
    m_deps_.caller.submit(m_request_,
                          [this](const commands::CommandResponse& resp)
                          { handle_response(resp); });
}

void MutationFlow::handle_response(const commands::CommandResponse& resp)
{
    if (resp.ok && resp.job.has_value()) {
        // A job-backed command: the toast points the operator at the Jobs
        // screen instead of claiming an immediate result.
        Toast t;
        t.kind = ToastKind::info;
        t.title = "job " + resp.job->id + " accepted";
        t.body = "[5] Jobs";
        t.source_command = m_command_name_;
        t.request_id = resp.id;
        t.raw_envelope = commands::serialize_response(resp);
        m_deps_.toasts.push(std::move(t));
    }
    else {
        m_deps_.toasts.push(
            toast_from_response(m_command_name_, resp.id, resp));
    }

    if (m_on_result_) {
        m_on_result_(resp);
    }
}

void run_mutation(
    MutationFlow& flow,
    commands::CommandRequest request,
    const CommandInfo& info,
    std::function<void(const commands::CommandResponse&)> on_result)
{
    flow.begin(std::move(request), info, std::move(on_result));
}

}  // namespace url_shortener::tui

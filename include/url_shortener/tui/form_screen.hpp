#pragma once

#include <functional>
#include <memory>
#include <string>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>
#include <url_shortener/tui/screen_id.hpp>

namespace url_shortener::tui
{

/**
 * @file form_screen.hpp
 * @brief The generic D5 form screen (subtask 06.0/08): a @ref IScreen that
 * hosts a @ref FormModel rendered by @ref make_form_component for any one
 * command, with its own confirm policy and toast queue. It is the "GenericForm"
 * node of D9/D11 and the reusable new/edit form of D5 (reused by the Links,
 * Tables and Import screens of 11-16).
 *
 * Built as a factory taking @ref ScreenContext&, like `make_settings_screen`
 * (subtask 21), because @ref App does not yet host real screens (the lifetime
 * restructuring is subtask 19); this keeps the screen headlessly testable with
 * a `MockCommandClient` + `InlinePoster`.
 */

/**
 * @brief Construction inputs for @ref make_form_screen.
 *
 * A value type the caller fills: which command the form edits, which screen id
 * it reports, an optional edit-prefill payload, and the success/cancel hooks
 * the screen cannot know on its own (what to refresh / where to navigate).
 */
struct FormScreenConfig
{
    ///< The command whose schema drives the form; copied into the owned
    ///< @ref FormModel.
    CommandInfo command;

    ///< The screen id this form reports (e.g. @ref ScreenId::link_form for the
    ///< new/edit link form, or @ref ScreenId::generic_form from the palette).
    ScreenId id = ScreenId::generic_form;

    ///< Human title shown on the form window (e.g. `"New link"` / `"Edit
    ///< link"`); the command name is appended in parentheses. Empty falls back
    ///< to the command name.
    std::string title;

    ///< Optional command `result` JSON to prefill the form for an edit flow
    ///< (via @ref FormModel::prefill_from_result); empty for a create form. A
    ///< raw server value - its display strings are sanitized by the view
    ///< (T-110).
    std::string prefill_result;

    ///< Called with the response after a successful submit so the caller can
    ///< refresh a table or navigate; empty when unused.
    std::function<void(const commands::CommandResponse&)> on_success;

    ///< Called when the operator cancels (Cancel button, Esc with a confirmed
    ///< discard); empty when unused.
    std::function<void()> on_cancel;
};

/**
 * @brief Creates the generic D5 form screen for one command.
 *
 * The screen owns a @ref FormModel built from @p config.command (prefilled when
 * @p config.prefill_result is set), a @ref ConfirmPolicy honoring
 * `--yes`, a @ref ToastQueue, and the @ref make_form_component body. All
 * submits go off the UI thread through @p context's @ref AsyncCaller; mutating
 * submits pass the confirm choke point (@ref run_mutation). The screen never
 * calls
 * @ref ICommandClient::call directly (ADR 0003 rule 1 / contract C8).
 * Text-field focus is mirrored into @ref ScreenContext::text_input_active.
 *
 * @param context The screen context providing the async caller, session,
 * router and options; its @ref ScreenContext::caller and router are references
 * owned by @ref App that must outlive the returned screen (captured by
 * reference).
 * @param config The command, id, title, optional prefill and success/cancel
 * hooks; moved into the screen.
 * @return An owning @ref IScreen for @p config.id; never null.
 */
std::unique_ptr<IScreen> make_form_screen(ScreenContext& context,
                                          FormScreenConfig config);

}  // namespace url_shortener::tui

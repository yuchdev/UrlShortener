#pragma once

#include <functional>
#include <memory>

#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/screen.hpp>

namespace url_shortener::tui
{

/**
 * @file generic_command_screen.hpp
 * @brief The generic command screen of D9/D11 (subtask 06.0/11): the
 * `Palette -> GenericForm -> Confirm -> result` node. For a command picked from
 * the Command Palette it hosts the generated D5 form (@ref make_form_component),
 * a raw-JSON args escape hatch (`Ctrl-R`; the spec named `Ctrl-J`, but FTXUI
 * maps that to the same byte as Enter, so a non-aliasing key is used), and a
 * scrollable result viewer that pretty-prints the response's raw `result` JSON.
 *
 * Every mutating submit - the generated form, the raw-JSON path, and a no-arg
 * mutating command - goes through @ref run_mutation with the session's
 * read-only flag, so none reaches @ref AsyncCaller::submit without the
 * capability/confirm choke point (ADR 0003 rule 4, threat model T-116). The
 * screen builds no JSON except through the sanctioned scanner
 * (`json_view::reserialize_object`) for the raw-JSON path, and never calls
 * @ref ICommandClient::call directly (contracts C1/C8). Every displayed
 * server/stored string is sanitized before rendering (threat model T-110).
 */

/**
 * @brief Construction inputs for @ref make_generic_command_screen.
 */
struct GenericCommandConfig
{
    ///< The command to run; its schema drives the generated form and its name
    ///< the submitted request. Copied into the screen.
    CommandInfo command;

    ///< Called when the operator leaves the command (Cancel / Esc, or `Esc`
    ///< from the result viewer) so the caller (the Palette) can return to its
    ///< list; empty when unused.
    std::function<void()> on_close;
};

/**
 * @brief Creates the generic command screen for one command.
 *
 * The screen owns a @ref FormModel for @p config.command, a @ref ConfirmPolicy
 * honoring `--yes`, a @ref ToastQueue, the @ref make_form_component body and a
 * @ref MutationFlow for the raw-JSON path. On a successful immediate result it
 * switches to a @ref ResultViewerModel view; a `job` response raises the job
 * toast and leaves the form open; an error raises the error toast with the form
 * open (D9 step 5). `Ctrl-R` toggles a raw-JSON args editor validated as exactly
 * one well-formed object before submission.
 *
 * @param context The screen context providing the async caller, session, router
 * and options; its @ref ScreenContext::caller and router are references owned by
 * @ref App that must outlive the returned screen (captured by reference).
 * @param config The command and the close hook; moved into the screen.
 * @return An owning @ref IScreen for @ref ScreenId::generic_form; never null.
 */
std::unique_ptr<IScreen> make_generic_command_screen(
    ScreenContext& context, GenericCommandConfig config);

}  // namespace url_shortener::tui

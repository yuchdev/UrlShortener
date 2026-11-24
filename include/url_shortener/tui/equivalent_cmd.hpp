#pragma once

#include <string>

#include <url_shortener/commands/envelope.hpp>

namespace url_shortener::tui
{

/**
 * @file equivalent_cmd.hpp
 * @brief The `Equivalent:` renderer (D5 footer): the exact CLI line and request
 * envelope a form/palette would send, as a teaching aid and the visible proof
 * that every UI runs the same command.
 *
 * FTXUI-free (`url_shortener_tui_model`). Both functions return display text
 * the render sink then passes through `text_fit::sanitize_for_terminal`
 * (T-110). Neither echoes a secret: command args carry none (contract C6), and
 * as defence in depth a redact-worthy argument key (one containing `password`,
 * `secret`, `token`, `dsn`, or `key`) has its value replaced by `"***"` before
 * it is shown (threat model T-125). @ref equivalent_cli additionally
 * single-quotes the JSON so a copy-pasted line cannot break out of its quoting.
 */

/**
 * @brief Renders the CLI command equivalent of a request.
 *
 * Produces `url_shortener cmd <command> --json '<args>'`, matching the command
 * catalog's CLI example form. The `<args>` object is single-quote-escaped for
 * POSIX shells: it is wrapped in single quotes and every embedded `'` is
 * rendered as `'\''`, so neither a quote, a backslash, nor any JSON content can
 * break out of the quoting (threat model T-125). Empty args render as
 * `--json '{}'`. Redact-worthy argument keys have their values replaced by
 * `"***"` before quoting. On Windows PowerShell the single-quote form is still
 * shown; adapting the quoting for a different shell is the operator's job (a
 * documented caveat).
 *
 * @param r The request to render; @ref commands::CommandRequest::command and
 * @ref commands::CommandRequest::args_json are used.
 * @return The single-line CLI command text (not yet terminal-sanitized; the
 * render sink does that, T-110).
 */
std::string equivalent_cli(const commands::CommandRequest& r);

/**
 * @brief Renders the exact v1 request envelope of a request.
 *
 * Produces `{"v":<v>,"id":"<id>","command":"<command>","args":<args>}` with an
 * `"options"` object appended only when `dry_run` or `async` is set; `"id"` is
 * omitted when empty. The `<args>` object is embedded with redact-worthy values
 * replaced by `"***"` (T-125) and is otherwise verbatim. `command` and `id` are
 * JSON-escaped.
 *
 * @param r The request to render.
 * @return The single-line request-envelope JSON text (not yet terminal-
 * sanitized; the render sink does that, T-110).
 */
std::string equivalent_envelope(const commands::CommandRequest& r);

}  // namespace url_shortener::tui

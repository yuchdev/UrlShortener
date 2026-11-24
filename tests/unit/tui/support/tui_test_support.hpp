#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/tui/command_client.hpp>
#include <url_shortener/tui/equivalent_cmd.hpp>
#include <url_shortener/tui/in_process_client.hpp>

#include "mock_command_client.hpp"

/**
 * @file tui_test_support.hpp
 * @brief Free-function ergonomics layered *over* the final 06.0
 * @ref url_shortener::tui::testing::MockCommandClient, for the 07.0 TUI tests.
 *
 * The 06.0 @ref MockCommandClient is shipped and `final`; it records
 * `commands::CommandRequest` structs (not text) and is driven by 57 existing
 * tests, so it cannot be extended in place. This header adds the small gap the
 * 07.0 screen/async tests need without touching the mock (Task 07.0 subtask
 * 22):
 *
 * - **Envelope builders** (@ref ok_envelope / @ref err_envelope) that return a
 *   `commands::CommandResponse` ready for `MockCommandClient::enqueue`, so a
 *   test scripts a success or a precise error without hand-building envelopes.
 * - A **`command.list` fixture** (@ref command_list_fixture /
 *   @ref command_list_infos) built from a *real*
 *   `composition::build_command_runtime` registry over the in-memory backend,
 * so form/palette tests track real `ArgSpec`/descriptor changes instead of a
 *   hand-copied catalog that can silently drift.
 * - **Request-text helpers** (@ref request_text / @ref request_texts /
 *   @ref request_texts_for) that serialize the mock's recorded structs into
 *   byte-exact request-envelope text for the "exact envelope" assertions
 *   (ADR 0003 rule 6), plus @ref expect_no_calls for read-only safety proofs.
 *
 * Test-support only, header-only, and deliberately **FTXUI-free** so the
 * FTXUI-less model-library tests (`url_shortener_tui_model`) link against it as
 * well as the render tests. There is exactly one mock class in the tree and one
 * TUI test-registration scheme; this header adds neither a second mock nor a
 * second CMake list.
 */

namespace url_shortener::tui::testing
{

/**
 * @brief Builds a success response carrying a verbatim result payload, ready
 * for
 * @ref MockCommandClient::enqueue.
 *
 * Thin, allocation-copy wrapper over `commands::make_success`: it sets `ok` and
 * `result_json` and leaves `job`/`error` empty. The payload is **not**
 * validated here (validation happens in `commands::serialize_response`), so a
 * test may script any bytes it wants the screen under test to receive.
 *
 * @param id Correlation id to echo in the response; copied into the envelope.
 * @param result_json Exact JSON text of the result, embedded verbatim; copied.
 * @return A `commands::CommandResponse` with `ok == true` and `result_json`
 * set.
 */
inline commands::CommandResponse ok_envelope(std::string_view id,
                                             std::string_view result_json)
{
    return commands::make_success(std::string(id), std::string(result_json));
}

/**
 * @brief Builds a failure response for a named wire error code, ready for
 * @ref MockCommandClient::enqueue (the as-built form of failure injection:
 * `enqueue(command, err_envelope(...))`).
 *
 * @p code is resolved through `commands::error_code_from_name`, so the envelope
 * carries exactly the `CommandErrorCode` the TUI will branch on. The resolved
 * code name is reused as the (fixed, value-free) error message so a serialized
 * fixture is self-describing.
 *
 * @param code Wire `error.code` name, e.g. `"not_found"` or `"invalid_field"`.
 * An **unrecognized** name fails closed to
 * `commands::CommandErrorCode::internal` (so a typo surfaces as a loud internal
 * error, never as a false `none`/success); the original text is still carried
 * as the message.
 * @param id Correlation id to echo in the response; copied into the envelope.
 * @return A `commands::CommandResponse` with `ok == false` and `error` set.
 */
inline commands::CommandResponse err_envelope(std::string_view id,
                                              std::string_view code)
{
    const std::optional<commands::CommandErrorCode> parsed =
        commands::error_code_from_name(code);
    const commands::CommandErrorCode resolved =
        parsed.value_or(commands::CommandErrorCode::internal);
    return commands::make_failure(
        std::string(id), commands::make_error(resolved, std::string(code)));
}

namespace detail
{

/**
 * @brief Builds an in-process TUI client over a real, freshly-built command
 * runtime on the in-memory backend, and runs @p fn against it.
 *
 * Centralizes the lifetime contract of `composition::build_command_runtime`:
 * the returned @ref composition::CommandRuntime borrows the `ServerConfig` and
 * the
 * @ref app::LinkBackendHandle by reference, and @ref tui::InProcessClient
 * borrows the runtime's registry, so all four must outlive @p fn. They are kept
 * alive on this stack frame for exactly the duration of the call; @p fn must
 * therefore return an owning **copy** (every public helper here returns a
 * self-owning value type), never a reference into the client or runtime.
 *
 * The client is passed non-`const` because `InProcessClient::call` is a
 * non-`const` override (@ref command_list_fixture executes `command.list`
 * through it); `commands()` is `const` and works through the same reference.
 * The caller runs as the local-operator `Admin` so `command.list` and the full
 * descriptor catalog are visible; nothing is mutated, so no storage state leaks
 * between calls.
 *
 * @tparam Fn Callable invoked as `fn(tui::InProcessClient&)`.
 * @param fn The work to run against the live client; its return value is
 * forwarded to the caller.
 * @return Whatever @p fn returns (by value).
 */
template<typename Fn>
auto with_real_command_client(Fn&& fn)
    -> decltype(fn(std::declval<InProcessClient&>()))
{
    const ServerConfig config;
    app::LinkBackendHandle backend;  // inactive handle -> in-memory backend
    const composition::CommandRuntime runtime =
        composition::build_command_runtime(config, backend);

    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.control_set = ControlSet::Admin;
    caller.transport = "tui";
    InProcessClient client(*runtime.registry, caller);
    return fn(client);
}

}  // namespace detail

/**
 * @brief Returns the real command catalog as `commands::CommandResponse`, i.e.
 * the `command.list` envelope a remote client's `commands()` path would
 * consume.
 *
 * Executes the registered `command.list` command against a real
 * `composition::build_command_runtime` registry over the in-memory backend, so
 * the fixture's `result_json` tracks live descriptor/`ArgSpec` changes (e.g. a
 * new `link.create` argument) instead of a hand-copied snapshot.
 *
 * @return The `command.list` response (`ok == true`, `result_json` carrying the
 * catalog); a self-owning value safe to retain after the runtime is gone.
 */
inline commands::CommandResponse command_list_fixture()
{
    return detail::with_real_command_client(
        [](InProcessClient& client)
        {
            commands::CommandRequest request;
            request.v = 1;
            request.id = "command-list-fixture";
            request.command = "command.list";
            request.args_json = "{}";
            return client.call(request);
        });
}

/**
 * @brief Returns the same real catalog as @ref command_list_fixture, but
 * already mapped to @ref CommandInfo for @ref MockCommandClient::set_commands.
 *
 * Built from the live registry via `tui::InProcessClient::commands`, so a
 * screen driven by the mock sees the real descriptors (permissions, arg kinds,
 * `mutating`/`is_job` flags) without the mock needing a registry of its own.
 *
 * @return One @ref CommandInfo per registered command, name-sorted; a
 * self-owning copy safe to retain after the runtime is gone.
 */
inline std::vector<CommandInfo> command_list_infos()
{
    return detail::with_real_command_client([](InProcessClient& client)
                                            { return client.commands(); });
}

/**
 * @brief Serializes one recorded @ref commands::CommandRequest to its exact v1
 * request-envelope text (ADR 0003 rule 6).
 *
 * Delegates to the single, product-owned request serializer
 * `tui::equivalent_envelope` (the same bytes the TUI shows as its
 * `Equivalent:` proof), so these "exact envelope" assertions can never drift
 * from what the UI renders. As documented there, a redact-worthy argument key
 * (one containing `password`/`secret`/`token`/`dsn`/`key`) has its value
 * replaced by `"***"`, and an empty `id` is omitted; TUI test requests carry no
 * secrets (contract C6), so the output is otherwise byte-for-byte verbatim.
 *
 * @param request The recorded request to serialize.
 * @return The single-line request-envelope JSON text.
 */
inline std::string request_text(const commands::CommandRequest& request)
{
    return equivalent_envelope(request);
}

/**
 * @brief Serializes every request the mock recorded, in call order, to exact
 * envelope text.
 *
 * Reads @ref MockCommandClient::recorded_requests (a thread-safe copy taken
 * under the mock's lock), then @ref request_text on each, so it is safe to call
 * after any worker thread has joined.
 *
 * @param mock The mock whose recorded calls to serialize. Not owned.
 * @return One envelope string per recorded request, preserving call order;
 * empty when nothing was recorded.
 */
inline std::vector<std::string> request_texts(const MockCommandClient& mock)
{
    std::vector<std::string> texts;
    const std::vector<commands::CommandRequest> recorded =
        mock.recorded_requests();
    texts.reserve(recorded.size());
    for (const commands::CommandRequest& request : recorded) {
        texts.push_back(request_text(request));
    }
    return texts;
}

/**
 * @brief Serializes only the recorded requests whose command name equals
 * @p command, in call order, to exact envelope text.
 *
 * The filtered form of @ref request_texts for asserting what a single command
 * put on the wire (e.g. every `link.list` a screen issued) while ignoring
 * unrelated catalog/session traffic.
 *
 * @param mock The mock whose recorded calls to filter and serialize. Not owned.
 * @param command Exact dotted command name to match.
 * @return One envelope string per matching request, in call order; empty when
 * no recorded request named @p command.
 */
inline std::vector<std::string> request_texts_for(const MockCommandClient& mock,
                                                  std::string_view command)
{
    std::vector<std::string> texts;
    for (const commands::CommandRequest& request : mock.recorded_requests()) {
        if (request.command == command) {
            texts.push_back(request_text(request));
        }
    }
    return texts;
}

/**
 * @brief Asserts, via Boost.Test, that the mock recorded no calls at all.
 *
 * The read-only safety predicate (threat model T-116): after driving a
 * read-only screen, a test calls this to prove no request whatsoever reached
 * the client. On a used mock it fails with the count and the offending command
 * names so the stray call is identifiable. Uses `BOOST_CHECK_MESSAGE`
 * (non-fatal) so a caller may assert further context after it.
 *
 * @param mock The mock expected to be untouched. Not owned.
 */
inline void expect_no_calls(const MockCommandClient& mock)
{
    const std::vector<commands::CommandRequest> recorded =
        mock.recorded_requests();
    std::string offenders;
    for (const commands::CommandRequest& request : recorded) {
        if (!offenders.empty()) {
            offenders += ", ";
        }
        offenders += request.command;
    }
    BOOST_CHECK_MESSAGE(recorded.empty(),
                        "expected no recorded calls but found "
                            << recorded.size() << " [" << offenders << "]");
}

}  // namespace url_shortener::tui::testing

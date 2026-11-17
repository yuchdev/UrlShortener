#pragma once

#include <iosfwd>
#include <string_view>

#include <url_shortener/commands/transport_adapter.hpp>
#include <url_shortener/composition/transport_factory.hpp>

namespace url_shortener::transport
{

/**
 * @brief The `serve-stdio` NDJSON stream transport: supplies the local-operator
 * @ref url_shortener::commands::CallerContext, runs one
 * @ref url_shortener::commands::NdjsonSession over a pair of iostreams, and
 * defines the EOF-driven shutdown contract of the mode (ADR 0002 section 4,
 * Task 05.0 subtask 15).
 *
 * This is the only stream transport in the milestone and the first concrete
 * @ref url_shortener::commands::ITransportAdapter. It holds no sockets and never
 * constructs `HttpServer` or touches `uri.txt` (contract C5); the header pulls
 * in no Beast/Asio. All business rules, DTO mapping and authorization stay
 * behind @ref url_shortener::commands::CommandRegistry::execute (contracts
 * C1/C2, C7); the adapter only frames in, calls, frames out, and drains jobs.
 *
 * **Caller identity is fixed by composition, never by the stream (T-107,
 * T-95).** @ref run attaches a `"local-operator"` caller over transport
 * `"stdio"` whose `control_set` is `Admin`, downgraded to `User` only when
 * @ref url_shortener::composition::TransportOptions::read_only is set
 * (`serve-stdio --read-only`, ADR 0002 "Authorization"). The role is derived
 * solely from construction-time options; no byte of the input stream can
 * escalate it, and the adapter grants no filesystem (`path`) capability.
 *
 * **stdout carries response envelopes only (T-102).** On entry @ref run forces
 * the process logger off `LogSink::Stdout` (overriding it to `LogSink::Stderr`)
 * so neither the adapter's own `warn` line nor any library log can interleave
 * with the wire. The full CLI-driven forcing is subtask 17; this guard ensures
 * the adapter itself can never emit a log line to stdout.
 *
 * **Shutdown semantics (`serve-stdio` contract, also documented in subtask
 * 19):**
 *
 * | Event | Behavior |
 * |---|---|
 * | stdin EOF | stop reading; drain jobs (below); exit `0`. |
 * | client reads stdout only after closing stdin | still works; earlier
 *   responses were already flushed; jobs finishing during the drain produce no
 *   output. |
 * | stdout closed / broken pipe | `output_failed`; cancel jobs; exit `4`
 *   (requires `SIGPIPE` ignored, wired in subtask 17; without it a closed pipe
 *   raises the signal and the default disposition terminates the process). |
 * | SIGINT / SIGTERM | no handler installed on purpose; the default disposition
 *   terminates the process immediately. In-memory jobs are lost (documented
 *   non-goal, T-106); rows already created stay persisted. A parent must close
 *   stdin for a graceful stop. |
 * | unrecoverable registry fault | not possible: `std::exception`s are mapped
 *   to a response by the session, and any that still escapes is caught by
 *   @ref run and mapped to exit `4`. |
 *
 * No signal handler is installed and no reader thread is spawned: a portable
 * interruptible blocking read on stdin does not exist (POSIX `SA_RESTART`
 * semantics, non-interruptible Windows console reads), so EOF-as-shutdown is the
 * documented contract rather than signal handling.
 *
 * **Lifetime / threading.** Single-threaded and driven by one thread. @ref run
 * owns the loop for the whole transport and is callable **exactly once** per
 * instance (a second call is a programming error, asserted). The streams and
 * the @ref url_shortener::commands::CommandRegistry handed to @ref run, and the
 * `job_runner` in the options, must all outlive the @ref run call.
 */
class StdioAdapter final : public commands::ITransportAdapter
{
public:
    /**
     * @brief Binds the adapter to its input/output streams and options.
     *
     * @param in Untrusted request source, framed one NDJSON line per request;
     * never logged. Captured by reference and must outlive this adapter.
     * @param out Response sink; only response envelopes are ever written here
     * (contract C2). Captured by reference and must outlive this adapter.
     * @param options Construction-time caller role, line cap, drain timeout and
     * the non-owning process-wide job runner; copied into the adapter. Any
     * `job_runner` the options name must outlive this adapter.
     */
    StdioAdapter(std::istream& in,
                 std::ostream& out,
                 const composition::TransportOptions& options);

    /**
     * @brief The stable transport identifier.
     * @return The literal `"stdio"`. The backing storage is a string literal and
     * outlives the adapter. `noexcept` via the base contract.
     */
    std::string_view name() const override;

    /**
     * @brief Runs the NDJSON session to a terminal condition, drains in-flight
     * jobs, and returns a process exit code.
     *
     * Builds the fixed local-operator caller (T-107), forces the logger off
     * stdout (T-102), then runs one @ref url_shortener::commands::NdjsonSession
     * over the bound streams. On the session's terminal reason it drains the job
     * runner (only when `options.job_runner != nullptr`) and maps to an exit
     * code from the shared catalog (0 success, 4 internal):
     *
     * | Session end | Drain | Exit |
     * |---|---|---|
     * | `eof` | `wait_idle(drain_timeout)`; on timeout `cancel_all()` + 5 s
     *   grace + one `warn` `stdio_drain_timeout` (counts only); then
     *   `shutdown()`. | `0` |
     * | `input_failed` | `cancel_all()` immediately + 5 s grace + `shutdown()`
     *   (no long wait). | `4` |
     * | `output_failed` | as `input_failed`. | `4` |
     *
     * Any `std::exception` that escapes the session (for example `bad_alloc`
     * while serializing a response, or a stream exception if masks were ever
     * enabled) is caught here, drained as a failure, and mapped to exit `4`;
     * the adapter never allows an exception to reach `std::terminate`.
     *
     * Must be called exactly once; a second call is a programming error
     * (asserted in debug, returns `4` otherwise).
     *
     * @param registry The process-wide command catalog (built by
     * `composition::build_command_runtime`, contract C11); the sole dispatch and
     * authorization entry point. Must outlive this call.
     * @return `0` on a clean EOF shutdown after any in-flight work drains; `4`
     * on a hard input error, an output-stream failure, an escaping exception, or
     * a second @ref run call.
     */
    int run(commands::CommandRegistry& registry) override;

private:
    /**
     * @brief Drains the job runner at shutdown, then stops it.
     *
     * A no-op when `options.job_runner == nullptr`. Otherwise:
     * - graceful (clean EOF): `wait_idle(drain_timeout)`; if that times out with
     *   work still outstanding, `cancel_all()` then a bounded 5 s grace
     *   `wait_idle`, and one `warn` `stdio_drain_timeout` carrying only the
     *   cancelled-job count (contract C6);
     * - non-graceful (a stream failure already stopped the session): skip the
     *   long wait, `cancel_all()` immediately, then the 5 s grace.
     *
     * Always finishes with `JobRunner::shutdown()` so queued jobs never run and
     * workers are joined; `shutdown()` is idempotent, so the subtask-17 caller
     * may call it again. A cooperative body stuck ignoring its cancel flag can
     * still block `shutdown()`'s unconditional join (documented residual risk on
     * @ref url_shortener::commands::JobRunner::shutdown).
     *
     * @param graceful `true` on a clean EOF (wait for the full drain window
     * first); `false` on a stream failure (cancel immediately).
     */
    void drain_jobs(bool graceful);

    ///< Untrusted request source; owned by the caller, not this adapter.
    std::istream& m_in;

    ///< Response sink; owned by the caller. Only envelopes are written here.
    std::ostream& m_out;

    ///< Construction-time caller role, line cap, drain timeout and non-owning
    ///< job runner pointer; copied at construction.
    composition::TransportOptions m_options;

    ///< `true` once @ref run has been entered; enforces run-exactly-once.
    bool m_has_run = false;
};

} // namespace url_shortener::transport

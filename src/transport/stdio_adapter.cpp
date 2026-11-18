/**
 * @file stdio_adapter.cpp
 * @brief The `serve-stdio` NDJSON stream transport (Task 05.0 subtask 15): the
 * local-operator caller, the EOF-driven job drain and the exit-code contract.
 *
 * No POSIX interrupt API is used here on purpose (success criterion 3): EOF on
 * stdin is the only graceful stop, and broken-pipe handling is the subtask-17
 * process wiring, not the adapter's job.
 */

#include "url_shortener/transport/stdio_adapter.hpp"

#include <cassert>
#include <chrono>
#include <exception>
#include <istream>
#include <ostream>
#include <string>

#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/commands/ndjson_session.hpp>
#include <url_shortener/observability/logger.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/observability/logging_config.hpp>
#include <url_shortener/security/control_set.hpp>

namespace url_shortener::transport
{

namespace
{

/// Exit code for an internal failure, read from the shared error catalog
/// (`internal` -> 4): a hard input error, a broken output stream or an escaping
/// exception. Deliberately not `1`, which the catalog reserves for `not_found`.
constexpr int k_exit_internal = 4;

/// Bounded grace window a cancelled drain waits for cooperative bodies to reach
/// a terminal state before @ref url_shortener::commands::JobRunner::shutdown
/// joins the workers.
constexpr std::chrono::seconds k_cancel_grace{5};

/**
 * @brief Cheap debug-only guard that the process logger is not on
 * `LogSink::Stdout` when the stdio session starts (threat model T-102,
 * carry-forward 1).
 *
 * `serve-stdio` emits only response envelopes to stdout; a single log line on
 * the same descriptor would corrupt the wire and could leak diagnostics. The
 * forcing of the logger onto `LogSink::Stderr` is done once, at serve-stdio
 * startup (`composition::RunServeStdio`), **before** the job runtime and its
 * workers are built, which is the correct place for a process-global, one-time
 * override. This adapter therefore no longer re-initializes the logger (which
 * could race a `Logger` component already created by a worker); it only asserts
 * that the caller honored the contract. A direct adapter user (the unit tests)
 * leaves the logger at its default `LogSink::Stderr`, so the assertion holds;
 * in release builds it compiles to nothing.
 */
void guard_logger_off_stdout()
{
    assert(url_shortener::observability::LoggerFactory::Config().sink
               != url_shortener::observability::LogSink::Stdout
           && "serve-stdio must force the logger off LogSink::Stdout before run");
}

} // namespace

StdioAdapter::StdioAdapter(std::istream& in,
                           std::ostream& out,
                           const composition::TransportOptions& options)
    : m_in(in), m_out(out), m_options(options)
{
}

std::string_view StdioAdapter::name() const
{
    return "stdio";
}

void StdioAdapter::drain_jobs(bool graceful)
{
    if (m_options.job_runner == nullptr) {
        return;
    }
    commands::JobRunner& runner = *m_options.job_runner;

    bool idle = false;
    if (graceful) {
        idle = runner.wait_idle(m_options.drain_timeout);
        if (!idle) {
            const std::size_t cancelled = runner.cancel_all();
            idle = runner.wait_idle(k_cancel_grace);
            observability::LoggerFactory::Component("stdio").warn(
                "stdio_drain_timeout",
                {{"cancelled", std::to_string(cancelled)}});
        }
    }
    else {
        runner.cancel_all();
        idle = runner.wait_idle(k_cancel_grace);
    }

    if (idle) {
        // Workers are idle, so shutdown()'s unconditional join returns promptly;
        // it is idempotent, so the subtask-17 process teardown may call it again.
        runner.shutdown();
    }
    else {
        // Carry-forward 3 / threat model T-88: a non-cooperative body ignored its
        // cancel flag and is still running after the grace window. shutdown()'s
        // unconditional join would block forever, so stay bounded: skip the join
        // here and return. The worker is joined by process teardown
        // (shutdown_process_job_runtime / ~JobRunner) once the body unblocks; a
        // body that never unblocks is the documented hard-signal non-goal
        // (T-106). The warn carries no request data (contract C6).
        observability::LoggerFactory::Component("stdio").warn(
            "stdio_drain_abandoned", {});
    }
}

int StdioAdapter::run(commands::CommandRegistry& registry)
{
    // Carry-forward 1: run-exactly-once. A second call is a programming error;
    // the registry-outlives-run precondition is documented on the header and on
    // ITransportAdapter and cannot be checked at runtime.
    assert(!m_has_run && "StdioAdapter::run must be called exactly once");
    if (m_has_run) {
        return k_exit_internal;
    }
    m_has_run = true;

    // T-102 (carry-forward 1): serve-stdio startup already forced the logger off
    // stdout before the runtime was built; assert the caller honored that.
    guard_logger_off_stdout();

    // T-107 / T-95: the caller role comes only from construction-time options,
    // never from the input stream; read_only downgrades Admin -> User, and no
    // filesystem (path) capability is granted.
    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = "stdio";
    caller.control_set =
        m_options.read_only ? ControlSet::User : ControlSet::Admin;

    commands::session_end end = commands::session_end::input_failed;
    try {
        // Carry-forward 2: max_line_bytes bounds every frame via the session.
        commands::NdjsonSession session(m_in, m_out, m_options.max_line_bytes);
        end = session.serve(registry, caller);
    }
    catch (const std::exception&) {
        // Carry-forward 5: no std::exception (e.g. bad_alloc in serialize, a
        // stream exception when masks are enabled) may reach std::terminate.
        // Treat as a failed session: cancel jobs, stop the runner, map to
        // internal.
        drain_jobs(/*graceful=*/false);
        return k_exit_internal;
    }
    catch (...) {
        // Carry-forward 2: likewise for a non-std exception (e.g. a thrown
        // non-exception type escaping a stream whose exception mask is enabled);
        // it must not reach std::terminate. Same failed-session handling.
        drain_jobs(/*graceful=*/false);
        return k_exit_internal;
    }

    switch (end) {
    case commands::session_end::eof:
        drain_jobs(/*graceful=*/true);
        return 0;
    case commands::session_end::input_failed:
    case commands::session_end::output_failed:
        drain_jobs(/*graceful=*/false);
        return k_exit_internal;
    }

    // Unreachable: session_end is exhaustively handled above.
    drain_jobs(/*graceful=*/false);
    return k_exit_internal;
}

} // namespace url_shortener::transport

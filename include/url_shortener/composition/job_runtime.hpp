#pragma once

#include <chrono>
#include <memory>

#include <url_shortener/commands/job_runner.hpp>

namespace url_shortener::composition
{

/**
 * @brief Construction-time settings for the one process-wide @ref
 * commands::JobRunner (milestone contract C11, decision log D13).
 *
 * A thin wrapper around @ref commands::JobRunnerOptions so a future tuning flag
 * (worker count / queue capacity) can grow the struct without changing the
 * @ref configure_process_job_runtime signature. The defaults are the V1 tuning
 * of @ref commands::JobRunnerOptions (two workers, a 16-deep queue, 256 retained
 * records).
 */
struct JobRuntimeSettings
{
    ///< Pool/queue/retention bounds forwarded verbatim to the @ref
    ///< commands::JobRunner constructor on first use; see @ref
    ///< commands::JobRunnerOptions.
    commands::JobRunnerOptions runner;
};

/**
 * @brief Records the settings the process-wide @ref commands::JobRunner will be
 * built with, before it is first used.
 *
 * Call once at startup (`main`, per mode) **before** the first
 * @ref process_job_runner / @ref build_command_runtime, which is where the
 * runner is lazily created. Thread-safe.
 *
 * @param settings The bounds the runner is constructed with on first use.
 * @return `true` when the settings were stored; `false` (and nothing changes)
 * when the runner already exists, because its bounds are fixed at construction
 * and cannot be re-tuned without a restart.
 */
bool configure_process_job_runtime(JobRuntimeSettings settings);

/**
 * @brief Returns the one process-wide @ref commands::JobRunner, creating it on
 * first use.
 *
 * The single job mechanism of the process (decision log D13): every
 * @ref CommandRuntime of the process shares this one runner (REST builds a
 * runtime per request, CLI/TUI/`serve-stdio` one per process), and registries
 * hold it as a `std::shared_ptr<commands::IJobSubmitter>` via
 * `set_job_submitter`, so a job submitted through one (possibly short-lived)
 * runtime outlives that runtime (threat model T-87). The runner is created on
 * first use over a process-lifetime `SystemClock` that outlives it, from the
 * bounds last passed to @ref configure_process_job_runtime (or the defaults).
 * Thread-safe: concurrent first-use callers get the same instance.
 *
 * Must not be shut down before @ref shutdown_process_job_runtime runs; the
 * returned `shared_ptr` keeps it alive for the caller regardless.
 *
 * @return The shared process-wide runner; never null.
 */
std::shared_ptr<commands::JobRunner> process_job_runner();

/**
 * @brief Drains, cancels and joins the process-wide job runner, once per
 * process, before the mode exits (server signal handler, `cmd`, `tui`,
 * `serve-stdio`).
 *
 * Performs, in order: `wait_idle(drain_timeout)` (bounded drain of outstanding
 * work), `cancel_all()` (queued jobs end `cancelled`, running cooperative
 * bodies are flagged) and `shutdown()` (joins every worker so no thread
 * outlives the process). Idempotent: a second call returns immediately. A no-op
 * when the runner was never created.
 *
 * @param drain_timeout How long to wait for in-flight work to finish before
 * cancelling the rest; the total wall time is bounded by this plus the time a
 * running body needs to observe its cancel flag (bodies poll once per row/page).
 * @return `true` when the drain completed within @p drain_timeout (or the runner
 * was never created / was already shut down); `false` when the timeout elapsed
 * with work still outstanding, which was then cancelled.
 */
bool shutdown_process_job_runtime(std::chrono::milliseconds drain_timeout);

/**
 * @brief Tests only: shuts the runner down and drops it, so the next
 * @ref process_job_runner creates a fresh one.
 *
 * Lets a unit test isolate the process-wide runner between cases. Never call it
 * in production. A no-op when no runner exists.
 */
void reset_process_job_runtime_for_tests();

}  // namespace url_shortener::composition

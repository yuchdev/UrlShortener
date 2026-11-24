#include <url_shortener/composition/job_runtime.hpp>

#include <memory>
#include <mutex>
#include <utility>

#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/core/clock.hpp>

namespace url_shortener::composition
{
namespace
{

/**
 * @brief The one process-lifetime clock the process-wide @ref
 * commands::JobRunner stamps records with.
 *
 * Intentionally heap-allocated and never deleted: the runner holds the clock by
 * `const IClock&`, so the clock must outlive the runner on **every** teardown
 * path, including static destruction when some other shared owner (a leaked or
 * late-destroyed @ref CommandRuntime) keeps the runner alive past this
 * translation unit's statics. A single trivially-small, never-freed object is
 * the standard idiom for such a process-lifetime dependency and sidesteps the
 * static-destruction-order hazard entirely.
 *
 * @return A reference to the process-lifetime clock; the same object every call.
 */
const IClock& process_clock()
{
    static SystemClock* const clock = new SystemClock();
    return *clock;
}

/**
 * @brief The mutable state behind the process-wide job runner: the pending
 * construction options, the lazily-created runner and a one-shot shutdown latch.
 *
 * Guarded as a unit by @ref mutex. The runner is created on first use and kept
 * (not dropped) after shutdown, so a late @ref process_job_runner returns the
 * same, already-stopped instance rather than silently spawning a second pool;
 * @ref reset_process_job_runtime_for_tests is the only path that drops it.
 */
struct JobRuntimeHolder
{
    ///< Serializes every read and mutation of the fields below.
    std::mutex mutex;

    ///< Bounds the runner is built with on first use; overwritten by
    ///< @ref configure_process_job_runtime while @ref runner is still null.
    commands::JobRunnerOptions options;

    ///< The one process-wide runner; null until first use, then retained for
    ///< the process lifetime (or until a test reset).
    std::shared_ptr<commands::JobRunner> runner;

    ///< `true` once @ref shutdown_process_job_runtime has drained/joined the
    ///< runner, so a second shutdown is a no-op; reset when a fresh runner is
    ///< created.
    bool shutdown_done = false;
};

/**
 * @brief Returns the single function-local @ref JobRuntimeHolder.
 * @return A reference to the process-wide holder; the same object every call.
 */
JobRuntimeHolder& holder()
{
    static JobRuntimeHolder instance;
    return instance;
}

}  // namespace

bool configure_process_job_runtime(JobRuntimeSettings settings)
{
    JobRuntimeHolder& state = holder();
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (state.runner) {
        // The runner's bounds are fixed at construction; refuse to change
        // settings that can no longer take effect (spec: returns false,
        // changes nothing).
        return false;
    }
    state.options = settings.runner;
    return true;
}

std::shared_ptr<commands::JobRunner> process_job_runner()
{
    JobRuntimeHolder& state = holder();
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.runner) {
        state.runner =
            std::make_shared<commands::JobRunner>(process_clock(),
                                                  state.options);
        state.shutdown_done = false;
    }
    return state.runner;
}

bool shutdown_process_job_runtime(std::chrono::milliseconds drain_timeout)
{
    std::shared_ptr<commands::JobRunner> runner;
    {
        JobRuntimeHolder& state = holder();
        const std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.runner || state.shutdown_done) {
            // Never created, or already drained/joined: nothing to do.
            return true;
        }
        runner = state.runner;
        state.shutdown_done = true;
    }

    // Drain/cancel/join off the holder lock: a running job body may itself touch
    // the composition layer, and the unconditional join in shutdown() blocks
    // until every cooperative body has observed its cancel flag and returned.
    const bool drained = runner->wait_idle(drain_timeout);
    runner->cancel_all();
    runner->shutdown();
    return drained;
}

void reset_process_job_runtime_for_tests()
{
    std::shared_ptr<commands::JobRunner> runner;
    {
        JobRuntimeHolder& state = holder();
        const std::lock_guard<std::mutex> lock(state.mutex);
        runner = std::move(state.runner);
        state.runner.reset();
        state.shutdown_done = false;
        state.options = commands::JobRunnerOptions {};
    }
    if (runner) {
        // Join the workers before releasing our reference so the next test
        // starts from a quiescent process (the destructor would also join, but
        // doing it here keeps the ordering explicit).
        runner->shutdown();
    }
}

}  // namespace url_shortener::composition

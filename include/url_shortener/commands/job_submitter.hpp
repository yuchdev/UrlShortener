#pragma once

#include <optional>
#include <string>

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>

namespace url_shortener::commands
{

/**
 * @brief The outcome of handing a prepared @ref JobBody to an
 * @ref IJobSubmitter.
 *
 * Exactly one side is meaningful, never both (decision log D13, subtask 08
 * constraint): on acceptance `job` holds the accepted job's wire shape and
 * `error.code == none`; on rejection `job` is empty and `error` carries the
 * reason (`busy` when the queue is full, `internal` on an unexpected runtime
 * failure, ...). This "either a job or an error" contract guarantees a rejected
 * or failed submission leaves no half-created job (threat model T-12): a
 * submitter that cannot enqueue must not report a `job`.
 */
struct JobSubmitResult
{
    ///< The accepted job's wire shape; set (and `error.code == none`) only when
    ///< the body was enqueued. `std::nullopt` on rejection.
    std::optional<JobInfo> job;

    ///< The rejection detail; `code != none` only when `job` is empty. Its
    ///< message carries no argument value (milestone contract C6). Default
    ///< (`none`) on acceptance.
    CommandError error;
};

/**
 * @brief The seam between @ref CommandRegistry and the one process-wide job
 * runtime (decision log D13).
 *
 * The registry never runs jobs itself; after authorization and validation it
 * hands a self-contained @ref JobBody to whatever submitter was installed via
 * @ref CommandRegistry::set_job_submitter. Task 05.0's `JobRunner` implements
 * this interface (05.0/02) and composition injects a single shared instance
 * (05.0/20); there is no second mechanism. When no submitter is installed, job
 * and async requests fail closed with `unsupported` and @ref submit is never
 * reached (threat model T-12).
 *
 * Only an `args_digest` (never raw `args_json`) travels with a submitted job,
 * so secrets passed as arguments are never persisted in the job record
 * (threat model T-13).
 */
class IJobSubmitter
{
public:
    /**
     * @brief Virtual destructor; the registry holds a `std::shared_ptr` to a
     * submitter through this base.
     */
    virtual ~IJobSubmitter() = default;

    /**
     * @brief Enqueues a prepared job body for asynchronous execution.
     *
     * Called on the executing (caller) thread after the request has been
     * authorized, its arguments validated and its @ref JobBody built by the
     * descriptor's @ref JobHandlerFactory. The implementation must return
     * quickly: it enqueues only and does not run the body on this thread. It
     * takes ownership of @p body. It must return **either** a job **or** an
     * error, never both (see @ref JobSubmitResult): a rejected submission must
     * not leave a half-created job.
     *
     * @param command The descriptor's command name, for the job record and
     * diagnostics; a validated dotted name, never raw caller input.
     * @param args_digest The first 16 hex characters of `SHA-256(args_json)`;
     * the only representation of the arguments that may travel with the job
     * (never the raw args, threat model T-13).
     * @param body The self-contained unit of work; owns every value it
     * operates on (the factory deep-copied them) and stays valid after the
     * originating request buffer is freed. Consumed by this call.
     * @return A @ref JobSubmitResult with `job` set on acceptance, or `error`
     * set (`busy`/`internal`/...) on rejection.
     */
    virtual JobSubmitResult submit(std::string command,
                                   std::string args_digest,
                                   JobBody body) = 0;
};

}  // namespace url_shortener::commands

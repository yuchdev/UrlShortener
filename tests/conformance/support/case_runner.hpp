#pragma once

/**
 * @file case_runner.hpp
 * @brief Replays corpus cases through one transport adapter.
 *
 * `CaseRunner` is transport-agnostic: it resolves per-case skips, materializes
 * `files` into a temp directory, drives the adapter step by step (substitute ->
 * send -> normalize -> match), captures `save` variables, polls `await_job`
 * jobs, and tears the temp directory down. Adding a transport is implementing
 * one @ref IConformanceAdapter; the runner and the corpus stay unchanged.
 */

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "adapter.hpp"
#include "corpus.hpp"
#include "matcher.hpp"

namespace conformance
{

/**
 * @brief Result of polling `job.get` to a terminal state (or timing out).
 *
 * Mirrors the Python harness `_poll_job` return `(response, finding)`:
 * @ref response holds the last `job.get` response seen (absent only when the
 * job id was never saved); @ref finding is empty on a terminal state and
 * carries the timeout / poll-fault reason otherwise.
 */
struct PollResult
{
    std::optional<Json> response;  ///< Last `job.get` response, if any.
    std::optional<std::string> finding;  ///< Timeout / fault reason, if any.
};

/**
 * @brief Aggregate outcome of replaying a corpus through one adapter.
 */
struct RunReport
{
    int ran = 0;  ///< Cases that executed at least one step.
    int skipped = 0;  ///< Cases skipped (only/skip/capability).
    int failed = 0;  ///< Cases with at least one failing step.

    ///< One `"transport/case/step[label]: <path>: <reason>"` line per failure,
    ///< each followed by the expected and actual JSON documents.
    std::vector<std::string> failures;

    ///< One `(case, reason)` pair per skipped case, for the coverage gate.
    std::vector<std::pair<std::string, std::string>> skips;
};

/**
 * @brief Drives corpus cases against a single transport adapter.
 */
class CaseRunner
{
public:
    /// Sleep hook (milliseconds); injectable so the self-test need not block.
    using SleepFn = std::function<void(int)>;

    /// Builds a runner whose `await_job` polling uses a real wall-clock sleep.
    CaseRunner();

    /**
     * @param sleep The sleep hook used between `await_job` polls.
     */
    explicit CaseRunner(SleepFn sleep);

    /**
     * @brief Replays every case in @p cases through @p adapter.
     *
     * After the run a summary JSON `{transport, ran, skipped:[{case,reason}]}`
     * is written to `$URLSHORTENER_CONFORMANCE_OUT` (or a temp file when unset)
     * for the coverage gate (subtask 15); a write failure is ignored.
     *
     * @param cases The parsed corpus cases.
     * @param adapter The transport adapter under test.
     * @return The aggregate @ref RunReport.
     */
    RunReport run(const std::vector<Case>& cases, IConformanceAdapter& adapter);

private:
    /**
     * @brief Replays one step; records a failure in @p report on mismatch.
     * @return `true` when the step passed, `false` when it failed.
     */
    bool run_step(IConformanceAdapter& adapter,
                  const std::string& transport_name,
                  const Case& test_case,
                  const Step& step,
                  std::size_t index,
                  const TransportCaps& caps,
                  VariableMap& vars,
                  RunReport& report);

    /**
     * @brief Polls `job.get` for the saved job id until terminal or timeout.
     *
     * Parity with the Python harness `_poll_job`: the job state is read from
     * `$.result.state` (the real `job.get` always returns the record under
     * `result`), the loop breaks only on a terminal state
     * (`succeeded`/`failed`/`cancelled`), the budget is `min(timeout_ms,
     * 60000)` ms, and waiting uses the injectable @ref SleepFn at a 25 ms
     * interval.
     *
     * @return A @ref PollResult carrying the last response and any timeout /
     * unsaved-variable / poll-fault finding.
     */
    PollResult poll_job(IConformanceAdapter& adapter,
                        const Step& step,
                        VariableMap& vars);

    /**
     * @brief Normalizes @p response, matches it against @p expect, and records
     * a failure line on mismatch.
     * @return `true` on a clean match.
     */
    bool check_expect(const std::string& where,
                      const Expect& expect,
                      const Json& response,
                      const Json* request,
                      const TransportCaps& caps,
                      const VariableMap& vars,
                      RunReport& report);

    SleepFn m_sleep;  ///< Poll-sleep hook.
};

}  // namespace conformance

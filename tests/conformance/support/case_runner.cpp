/**
 * @file case_runner.cpp
 * @brief Implementation of the conformance case runner (see case_runner.hpp).
 */

#include "case_runner.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <thread>

#include "matcher.hpp"

namespace conformance
{

namespace
{

/// Returns a process-unique temp directory path for a case run (not created).
std::filesystem::path unique_tmp_dir(const std::string& case_name)
{
    static std::atomic<unsigned long long> counter{0};
    const unsigned long long n = counter.fetch_add(1);
    return std::filesystem::temp_directory_path() /
           ("conformance_" + case_name + "_" + std::to_string(n));
}

/// Builds the expected envelope from an @ref Expect block (after substitution).
Json build_expected(const Expect& expect, const VariableMap& vars)
{
    Json envelope = Json::object();
    envelope.set("v", Json::integer(1));
    envelope.set("id", Json::string("<id>"));
    envelope.set("ok", Json::boolean(expect.ok));
    std::string substitute_error;
    Json body = Matcher::substitute(expect.value, vars, &substitute_error);
    switch (expect.body)
    {
        case Expect::Body::Result:
            envelope.set("result", std::move(body));
            break;
        case Expect::Body::Job:
            envelope.set("job", std::move(body));
            break;
        case Expect::Body::Error:
            envelope.set("error", std::move(body));
            break;
        case Expect::Body::None:
            break;
    }
    return envelope;
}

/// `true` when a job `state` string denotes a terminal job.
bool is_terminal_state(const std::string& state)
{
    return state == "succeeded" || state == "failed" || state == "cancelled" ||
           state == "canceled" || state == "done" || state == "completed" ||
           state == "complete" || state == "error";
}

/// Applies a step's `save` captures from @p response into @p vars.
void apply_save(const Step& step, const Json& response, VariableMap& vars)
{
    for (const auto& capture : step.save)
    {
        const Json* value = response.path(capture.second);
        if (value != nullptr)
        {
            vars[capture.first] = *value;
        }
    }
}

/// Writes the per-transport coverage summary; a write failure is ignored.
void write_summary(Transport transport, const RunReport& report)
{
    std::filesystem::path out;
    if (const char* dir = std::getenv("URLSHORTENER_CONFORMANCE_OUT"))
    {
        out = std::filesystem::path(dir) /
              (std::string(transport_id(transport)) + "_summary.json");
    }
    else
    {
        out = std::filesystem::temp_directory_path() /
              (std::string(transport_id(transport)) + "_summary.json");
    }

    Json summary = Json::object();
    summary.set("transport",
                Json::string(std::string(transport_id(transport))));
    summary.set("ran", Json::integer(report.ran));
    Json skipped = Json::array();
    for (const auto& entry : report.skips)
    {
        Json item = Json::object();
        item.set("case", Json::string(entry.first));
        item.set("reason", Json::string(entry.second));
        skipped.push_back(std::move(item));
    }
    summary.set("skipped", std::move(skipped));

    std::ofstream stream(out, std::ios::binary | std::ios::trunc);
    if (stream)
    {
        stream << summary.dump();
    }
}

}  // namespace

CaseRunner::CaseRunner()
    : m_sleep([](int ms)
              {
                  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
              })
{
}

CaseRunner::CaseRunner(SleepFn sleep) : m_sleep(std::move(sleep))
{
}

RunReport CaseRunner::run(const std::vector<Case>& cases,
                          IConformanceAdapter& adapter)
{
    RunReport report;
    const Transport transport = adapter.transport();
    const TransportCaps caps = adapter.caps();
    const std::string transport_name(transport_id(transport));

    for (const Case& test_case : cases)
    {
        // --- Skip resolution (only / skip / capability) ------------------
        if (!test_case.only.empty() &&
            test_case.only.find(transport) == test_case.only.end())
        {
            ++report.skipped;
            report.skips.emplace_back(test_case.name,
                                      "transport not in case.only");
            continue;
        }
        const auto skip_it = test_case.skip.find(transport);
        if (skip_it != test_case.skip.end())
        {
            ++report.skipped;
            report.skips.emplace_back(test_case.name, skip_it->second);
            continue;
        }
        bool needs_raw = false;
        for (const Step& step : test_case.steps)
        {
            if (step.raw_request.has_value())
            {
                needs_raw = true;
            }
        }
        if (needs_raw && !caps.carries_raw)
        {
            ++report.skipped;
            report.skips.emplace_back(
                test_case.name, "transport cannot carry raw_request");
            continue;
        }

        // --- Per-case temp dir + materialized files ----------------------
        const std::filesystem::path tmp = unique_tmp_dir(test_case.name);
        std::error_code ec;
        std::filesystem::create_directories(tmp, ec);
        for (const auto& file : test_case.files)
        {
            std::ofstream stream(tmp / file.first, std::ios::binary);
            stream << file.second;
        }

        const std::optional<std::string> declined =
            adapter.begin_case(test_case, tmp);
        if (declined.has_value())
        {
            ++report.skipped;
            report.skips.emplace_back(test_case.name, *declined);
            std::filesystem::remove_all(tmp, ec);
            continue;
        }

        // --- Replay steps -------------------------------------------------
        VariableMap vars;
        vars["tmp"] = Json::string(tmp.string());
        bool case_failed = false;
        for (std::size_t i = 0; i < test_case.steps.size() && !case_failed; ++i)
        {
            const Step& step = test_case.steps[i];
            case_failed = !run_step(adapter, transport_name, test_case, step, i,
                                    caps, vars, report);
        }
        if (case_failed)
        {
            ++report.failed;
        }
        else
        {
            ++report.ran;
        }

        adapter.end_case();
        std::filesystem::remove_all(tmp, ec);
    }

    write_summary(transport, report);
    return report;
}

bool CaseRunner::run_step(IConformanceAdapter& adapter,
                          const std::string& transport_name,
                          const Case& test_case,
                          const Step& step,
                          std::size_t index,
                          const TransportCaps& caps,
                          VariableMap& vars,
                          RunReport& report)
{
    // Build the request text (substituted) and the request object for N4.
    std::string substitute_error;
    std::optional<Json> request_json;
    std::string request_text;
    if (step.request.has_value())
    {
        request_json =
            Matcher::substitute(*step.request, vars, &substitute_error);
        request_text = request_json->dump();
    }
    else
    {
        const Json raw = Matcher::substitute(
            Json::string(*step.raw_request), vars, &substitute_error);
        request_text = raw.is_string() ? raw.as_string() : *step.raw_request;
    }

    const std::string label = step.label.empty()
                                  ? ("step[" + std::to_string(index) + "]")
                                  : ("step[" + step.label + "]");
    const std::string where = transport_name + "/" + test_case.name + "/" +
                              label;

    if (!substitute_error.empty())
    {
        report.failures.push_back(where + ": " + substitute_error);
        return false;
    }

    const std::string response_text = adapter.send(step, request_text);
    std::string parse_error;
    const std::optional<Json> response =
        Json::parse(response_text, &parse_error);
    if (!response.has_value())
    {
        report.failures.push_back(where + ": response is not valid JSON (" +
                                  parse_error + "): " + response_text);
        return false;
    }

    apply_save(step, *response, vars);

    const Json* request_ptr =
        request_json.has_value() ? &request_json.value() : nullptr;

    // Immediate accept assertion for an async job step.
    if (step.await_job.has_value() && step.expect_accept.present)
    {
        if (!check_expect(where + " (accept)", step.expect_accept, *response,
                          request_ptr, caps, vars, report))
        {
            return false;
        }
        const Json terminal =
            poll_job(adapter, step, vars);
        return check_expect(where, step.expect, terminal, nullptr, caps, vars,
                            report);
    }

    return check_expect(where, step.expect, *response, request_ptr, caps, vars,
                        report);
}

Json CaseRunner::poll_job(IConformanceAdapter& adapter,
                          const Step& step,
                          VariableMap& vars)
{
    const AwaitJob& await = *step.await_job;
    Json job_id = Json::null();
    const auto it = vars.find(await.var);
    if (it != vars.end())
    {
        job_id = it->second;
    }
    Json args = Json::object();
    args.set("id", job_id);
    Json request = Json::object();
    request.set("v", Json::integer(1));
    request.set("command", Json::string("job.get"));
    request.set("args", std::move(args));
    const std::string request_text = request.dump();

    Json last = Json::object();
    int elapsed = 0;
    while (elapsed <= await.timeout_ms)
    {
        const std::string text = adapter.send(step, request_text);
        std::string parse_error;
        std::optional<Json> doc = Json::parse(text, &parse_error);
        if (!doc.has_value())
        {
            break;
        }
        last = std::move(*doc);
        const Json* state = last.path("$.job.state");
        if (state == nullptr)
        {
            state = last.path("$.result.state");
        }
        const bool terminal =
            state != nullptr && state->is_string() &&
            is_terminal_state(state->as_string());
        if (terminal || last.path("$.result") != nullptr)
        {
            break;
        }
        m_sleep(25);
        elapsed += 25;
    }
    return last;
}

bool CaseRunner::check_expect(const std::string& where,
                              const Expect& expect,
                              const Json& response,
                              const Json* request,
                              const TransportCaps& caps,
                              const VariableMap& vars,
                              RunReport& report)
{
    std::vector<std::string> findings;
    const Json normalized =
        Matcher::normalize_actual(response, request, caps.carries_id, &findings);
    const Json expected = build_expected(expect, vars);
    const MatchResult result =
        Matcher::match(expected, normalized, expect.subset, vars);
    for (const std::string& reason : result.findings)
    {
        findings.push_back(reason);
    }
    if (findings.empty())
    {
        return true;
    }
    std::string message = where;
    for (const std::string& finding : findings)
    {
        message += "\n  " + finding;
    }
    message += "\n  expected: " + expected.dump();
    message += "\n  actual:   " + normalized.dump();
    report.failures.push_back(std::move(message));
    return false;
}

}  // namespace conformance

#pragma once

/**
 * @file corpus.hpp
 * @brief Corpus data model (`Case`/`Step`/`Expect`) and directory loader.
 *
 * Parses the `tests/conformance/commands` suite files into typed structures
 * the case
 * runner can replay. This loader performs only the **structural** checks needed
 * to build the model safely (valid JSON, required keys, unique case names);
 * full semantic validation is the linter's job
 * (`tools/conformance_lint.py`, decision log D3), not duplicated here.
 */

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "adapter.hpp"
#include "mini_json.hpp"

namespace conformance
{

/**
 * @brief An expected envelope assertion (an `expect` or `expect_accept` block).
 */
struct Expect
{
    /// Which body the envelope carries.
    enum class Body
    {
        None,    ///< No body parsed (absent block).
        Result,  ///< A success `result` object.
        Job,     ///< An accepted `job` object.
        Error    ///< An `error` object (only `code` is compared).
    };

    bool present = false;          ///< Whether the block was supplied.
    bool ok = false;               ///< Expected envelope `ok` flag.
    bool subset = false;           ///< `true` for `match: "subset"`.
    Body body = Body::None;        ///< Which of result/job/error is set.
    Json value = Json::object();   ///< The result/job/error object as authored.
};

/**
 * @brief Async-job coupling parsed from a step's `await_job` block.
 */
struct AwaitJob
{
    std::string var;         ///< Variable holding the job id to poll.
    int timeout_ms = 5000;   ///< Poll budget before the step is failed.
};

/**
 * @brief Resource-route equivalent call for the `rest_resource` transport.
 */
struct RestResource
{
    std::string method;               ///< HTTP method (e.g. `"POST"`).
    std::string path;                 ///< Route path.
    std::optional<Json> query;        ///< Optional query parameters object.
    std::optional<Json> body;         ///< Optional request body.
    std::optional<int> status;        ///< Expected HTTP status, if pinned.
};

/**
 * @brief One replayed step within a case.
 */
struct Step
{
    std::string label;                      ///< Shown in failure output.
    std::optional<std::string> caller;      ///< Per-step role override.
    std::optional<Json> request;            ///< Request envelope, or absent.
    std::optional<std::string> raw_request; ///< Verbatim text, or absent.
    Expect expect;                          ///< Required terminal assertion.
    Expect expect_accept;                   ///< Immediate `job` assertion.
    std::map<std::string, std::string> save;  ///< var -> JSONPath captures.
    std::optional<AwaitJob> await_job;      ///< Async-job polling, if any.
    std::optional<RestResource> rest_resource;  ///< Resource-route form.
    std::optional<int> http_status;         ///< REST generic status override.
    std::optional<int> cli_exit;            ///< CLI exit-code override.
};

/**
 * @brief One corpus case: a scenario replayed on a fresh store per transport.
 */
struct Case
{
    std::string name;                        ///< Globally unique case id.
    std::string doc;                         ///< One-line description.
    std::string command;                     ///< Primary command under test.
    std::string caller = "admin";            ///< Default role for all steps.
    std::string backend = "sqlite";          ///< Backing store selector.
    std::vector<Step> steps;                 ///< Ordered steps (>= 1).
    std::map<std::string, std::string> files;  ///< name -> text materialized.
    std::set<Transport> only;                ///< Allowed transports, if set.
    std::map<Transport, std::string> skip;   ///< transport -> skip reason.
    std::string suite;                       ///< Owning suite id.
};

/**
 * @brief Loads corpus cases from a directory of `*.json` suite files.
 */
class CorpusLoader
{
public:
    /**
     * @brief Loads every `*.json` file in @p dir, sorted by file name.
     *
     * @param dir Directory holding the corpus suite files.
     * @param error Optional out-parameter; set to a reason on the first
     * structural failure (bad JSON, missing required key, duplicate case name).
     * @return The parsed cases on success, or an empty vector on failure (with
     * @p error set). A duplicate case name across files is a failure.
     */
    static std::vector<Case> load_directory(const std::filesystem::path& dir,
                                            std::string* error);

    /**
     * @brief Parses a single suite document into its cases.
     *
     * Exposed for the self-test; shares all structural checks with
     * @ref load_directory.
     *
     * @param text The suite file contents.
     * @param source A label (usually the file name) used in error messages.
     * @param error Optional out-parameter set on failure.
     * @return The suite's cases, or an empty vector on failure.
     */
    static std::vector<Case> load_suite(const std::string& text,
                                        const std::string& source,
                                        std::string* error);
};

}  // namespace conformance

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/json_scan.hpp>

namespace url_shortener::commands
{

/**
 * @brief The two mode flags carried by the request envelope's `options` object.
 *
 * Both default to `false` (the catalog fixes exactly these two fields; any
 * other `options` key makes the envelope `invalid_envelope`). The flags are
 * only parsed here; whether a given command actually honors them is decided
 * later by the registry (`unsupported` when it does not).
 */
struct CommandOptions
{
    ///< `true` when the caller requested a validate-and-authorize-only run with
    ///< no mutation; mirrors `options.dry_run`. Honored only where the catalog
    ///< says so, `unsupported` elsewhere.
    bool dry_run = false;

    ///< `true` when the caller asked to run an async-capable command as a job;
    ///< mirrors `options.async`. Honored only where the catalog says so.
    bool async = false;
};

/**
 * @brief One parsed, envelope-validated command request (wire shape v1).
 *
 * Produced by @ref parse_request from a single request-envelope JSON text. The
 * envelope rules (version, id, command-name shape, `args` is an object, closed
 * `options`) have all passed by the time this struct exists; the *contents* of
 * `args` are still unvalidated (that is `ArgSpec`, a later subtask). Every
 * string field is safe to log and to echo: `id` is a validated client id or a
 * server-generated fallback, `command` matched @ref is_valid_command_name.
 */
struct CommandRequest
{
    ///< Envelope version; always `1` once parsed (the only version defined).
    int v = 1;

    ///< Correlation id echoed back in the response: either the client's `id`
    ///< after it passed the id rules, or a server-generated `"srv-<n>"` when
    ///< the client sent none. Never raw, unvalidated client input.
    std::string id;

    ///< Dotted command name, validated for shape only (@ref
    ///< is_valid_command_name); not yet resolved against the registry.
    std::string command;

    ///< Exact JSON source text of the `args` object, stored verbatim (never
    ///< re-serialized); `"{}"` when the envelope omitted `args`. Its contents
    ///< are not validated here.
    std::string args_json = "{}";

    ///< The parsed `options` flags; both `false` when `options` was omitted.
    CommandOptions options;
};

/**
 * @brief Hard bounds @ref parse_request applies to one request envelope.
 *
 * These are passed in (rather than read from `ServerConfig`) so the commands
 * layer stays free of server-config coupling; the adapter copies the matching
 * `ServerConfig` fields into this struct. The byte cap is defense in depth: the
 * REST adapter already enforces `max_request_body_bytes` before calling.
 */
struct envelope_limits
{
    ///< Maximum accepted envelope size in bytes, checked by the scanner before
    ///< parsing; a larger input is rejected as `too_large`. Mirrors
    ///< `ServerConfig::max_request_body_bytes`.
    std::size_t max_bytes = 65536;

    ///< Maximum accepted length of the client `id` in bytes; a longer `id` is
    ///< treated as absent for the echoed id and rejected as `invalid_envelope`.
    ///< Mirrors `ServerConfig::request_id_max_length`.
    std::size_t max_id_len = 64;

    ///< Maximum accepted length of the decoded `command` name in bytes; a
    ///< longer name is rejected as `invalid_envelope`.
    std::size_t max_command_len = 64;

    ///< Maximum JSON nesting depth passed through to the scanner; exceeding it
    ///< is rejected as `too_deep`. Mirrors the scanner's default.
    int max_depth = 8;
};

/**
 * @brief Outcome of @ref parse_request: either a validated request or a precise
 * `invalid_envelope` error, plus the id to echo in the response.
 *
 * Exactly one of two states holds: on success `request` has a value and `error`
 * is `none`; on failure `request` is empty and `error` carries
 * `invalid_envelope` with a fixed, value-free message. `id` is always populated
 * with the id the response must echo (see @ref id), so a caller can build an
 * error envelope without re-deriving the id.
 */
struct parse_request_result
{
    ///< The parsed request on success; empty on failure.
    std::optional<CommandRequest> request;

    ///< The failure detail when `request` is empty; `code == none` on success.
    ///< Always `invalid_envelope` on failure, with a fixed message naming only
    ///< the offending field, never echoing any input value.
    CommandError error;

    ///< The id to echo in the response in either outcome: the client's `id`
    ///< when it passed the id rules (1..`max_id_len` bytes, no control chars),
    ///< otherwise a server-generated `"srv-<n>"`. Never raw client input, so it
    ///< is safe to place in a response and an audit line even on failure.
    std::string id;

    /**
     * @brief Indicates whether parsing produced a request.
     * @return `true` when `request` has a value (success); `false` otherwise.
     */
    bool ok() const { return request.has_value(); }
};

/**
 * @brief Parses one request-envelope JSON text into a @ref CommandRequest.
 *
 * Enforces the envelope rules of docs/commands/README.md section 1 using the
 * one sanctioned scanner (`json_scan.hpp`): the text is a single JSON object;
 * `v` is the integer `1`; `command` is a string matching @ref
 * is_valid_command_name within `max_command_len`; an optional `id` is a string
 * of 1..`max_id_len` bytes with no control characters (absent -> @ref
 * next_server_id); an optional `args` is an object stored verbatim (absent ->
 * `"{}"`); an optional `options` object carries only boolean `dry_run`/`async`.
 * The envelope is closed: any unknown top-level or `options` key, and any
 * duplicate key, is rejected. No failure message echoes any substring of the
 * input.
 *
 * The id is resolved first, so a response to a *failed* parse still echoes the
 * client id when that id was itself valid; an absent, overlong or control-char
 * id is replaced by a `"srv-<n>"` fallback (never the hostile value).
 *
 * @param text The candidate envelope JSON. Treated as untrusted; never logged.
 * @param limits Byte/id-length/command-length/depth caps; defaults mirror the
 * server's request-hardening limits.
 * @return A @ref parse_request_result. On success `ok()` is `true`, `request`
 * holds the parsed command, and `error.code == none`. On failure `ok()` is
 * `false`, `request` is empty, and `error.code == invalid_envelope` with a
 * fixed message for the first rule violated (`malformed`, `too_large`,
 * `too_deep`, `duplicate_key`, or a field-naming message). In both outcomes
 * `id` holds the id to echo.
 */
parse_request_result parse_request(std::string_view text,
                                   const envelope_limits& limits = {});

/**
 * @brief Returns the next process-wide server-generated correlation id.
 *
 * Backed by a function-local `std::atomic<std::uint64_t>`, so the returned ids
 * are unique and strictly increasing across threads for the life of the
 * process. The id carries no meaning for the server and is not unguessable; it
 * exists only to correlate a response with a request that supplied no `id`.
 *
 * @return A fresh id of the form `"srv-<n>"` with a monotonically increasing
 * `<n>` starting at `1`.
 */
std::string next_server_id();

/**
 * @brief Tests whether @p name is a well-formed dotted command name.
 *
 * Accepts exactly the grammar `^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$`
 * (lower-case letters, digits and underscores in dot-separated segments, each
 * segment beginning with a letter, at least two segments). Implemented by hand;
 * no `<regex>`. This checks name *shape* only; it does not consult the registry
 * and so says nothing about whether such a command exists.
 *
 * @param name The candidate command name (already decoded from JSON).
 * @return `true` when @p name matches the grammar; `false` otherwise, including
 * for the empty string, an upper-case letter, a leading/trailing/doubled dot,
 * a single segment with no dot, or any other character.
 */
bool is_valid_command_name(std::string_view name);

/**
 * @brief Lifecycle state of a job, matching command-catalog section 4.
 *
 * Only the two wire-shape fields of an accepted job are modeled in this
 * subtask (@ref JobInfo); the runtime that transitions a job through these
 * states (`JobRunner`, `job.get/list/cancel`) lands in Task 05.0. The
 * enumerator order is the source order of the static table backing
 * @ref job_state_name and @ref job_state_from_name.
 */
enum class JobState
{
    ///< Accepted and waiting to run; the only state an envelope reports today.
    queued,

    ///< Currently executing on a worker (set by the runner, Task 05.0).
    running,

    ///< Finished successfully; a terminal state.
    succeeded,

    ///< Finished with an error; a terminal state.
    failed,

    ///< Terminated by `job.cancel`; a terminal state.
    cancelled
};

/**
 * @brief Maps a @ref JobState to its stable lower-case wire name.
 *
 * @param state Any @ref JobState enumerator.
 * @return The wire name (`"queued"`, `"running"`, `"succeeded"`, `"failed"`,
 * `"cancelled"`); a view into static storage valid for the program lifetime.
 * An out-of-range value (only producible by casting) maps to `"queued"`.
 */
std::string_view job_state_name(JobState state);

/**
 * @brief Maps a wire job-state name back to its @ref JobState enumerator.
 *
 * @param name Candidate wire name; compared exactly against each state's name.
 * @return The matching @ref JobState, or `std::nullopt` when @p name matches no
 * state (including the empty string).
 */
std::optional<JobState> job_state_from_name(std::string_view name);

/**
 * @brief The wire shape of an accepted asynchronous job (`{"id","state"}`).
 *
 * Carried by @ref CommandResponse when a command was accepted to run as a job
 * instead of producing an immediate result. This subtask models only the two
 * serialized fields; the job runtime is Task 05.0.
 */
struct JobInfo
{
    ///< Job id of the form `"j-<n>"`, assigned by the runner (Task 05.0).
    ///< Echoed verbatim and escaped on serialization; empty until assigned.
    std::string id;

    ///< Current lifecycle state; `queued` on a freshly accepted job.
    JobState state = JobState::queued;
};

/**
 * @brief One command response (wire shape v1): the inverse of a
 * @ref CommandRequest.
 *
 * Exactly one of three payloads is present, mirroring the catalog envelope:
 * an immediate `result` (pre-serialized JSON text produced once by the
 * handler), an accepted `job`, or an `error`. @ref serialize_response enforces
 * this invariant and the `ok`/payload consistency; a violation yields a
 * well-formed `internal` failure envelope rather than malformed JSON.
 */
struct CommandResponse
{
    ///< Envelope version; always `1` (the only version defined).
    int v = 1;

    ///< Correlation id echoed from the request; escaped on serialization.
    std::string id;

    ///< `true` for a result or an accepted job; `false` for an error.
    bool ok = false;

    ///< Pre-serialized JSON text of the result, embedded verbatim (never
    ///< re-encoded). Set iff `ok` and no `job`; empty otherwise. Milestone
    ///< contract C1: the handler produces this once.
    std::string result_json;

    ///< The accepted job's wire shape. Set iff `ok` and the command was run as
    ///< a job; `std::nullopt` otherwise.
    std::optional<JobInfo> job;

    ///< Failure detail. Set (`code != none`) iff `!ok`; default-constructed
    ///< (`none`) otherwise.
    CommandError error;
};

/**
 * @brief Builds a success response carrying a pre-serialized result payload.
 *
 * @param id Correlation id to echo; moved into the response.
 * @param result_json Exact JSON text of the result, produced by the handler;
 * moved in and embedded verbatim by @ref serialize_response. Must be exactly
 * one well-formed JSON value (validated under @ref k_result_scan_limits), else
 * serialization fails the invariant and emits an `internal` envelope.
 * @return A @ref CommandResponse with `ok == true` and `result_json` set.
 */
CommandResponse make_success(std::string id, std::string result_json);

/**
 * @brief Builds a success response reporting that the command was accepted as a
 * job.
 *
 * @param id Correlation id to echo; moved into the response.
 * @param job The accepted job's wire shape; moved in.
 * @return A @ref CommandResponse with `ok == true` and `job` set.
 */
CommandResponse make_job_accepted(std::string id, JobInfo job);

/**
 * @brief Builds a failure response carrying an error payload.
 *
 * @param id Correlation id to echo; moved into the response.
 * @param error The failure detail; moved in. Its `code` should not be `none`,
 * else serialization fails the invariant and emits an `internal` envelope.
 * @return A @ref CommandResponse with `ok == false` and `error` set.
 */
CommandResponse make_failure(std::string id, CommandError error);

/**
 * @brief Scan limits applied when @ref serialize_response validates a `result`
 * payload before embedding it verbatim.
 *
 * Deliberately looser than the request-envelope limits: a legitimate result
 * (a paged `link.list`, the rows of a `db` query) can be far larger and wider
 * than any request envelope, so a shared cap would reject valid results. The
 * values are a defense-in-depth bound on a trusted-but-possibly-buggy handler's
 * output, not a parse of untrusted client input.
 *
 * - `max_bytes` 1 MiB: upper bound on the size of the scanned payload.
 * - `max_depth` 16: nesting allowed inside the value.
 * - `max_members` 4096: top-level members/elements of the value.
 * - `max_key_len` 128: the scanner default (unchanged).
 *
 * The aggregate initializer order matches @ref scan_limits:
 * `{max_bytes, max_depth, max_members, max_key_len}`.
 */
inline constexpr scan_limits k_result_scan_limits {
    1024u * 1024u, 16, 4096u, 128u};

/**
 * @brief Serializes a @ref CommandResponse to the v1 response envelope JSON.
 *
 * Field order is fixed: `v`, `id`, `ok`, then exactly one of `result`, `job`
 * or `error`. `id` and `error.message` are escaped with
 * `url_shortener::jsonString` (the message is first run through
 * @ref sanitize_error_message); `result_json` is embedded verbatim and is never
 * re-escaped. `error` renders as `{"code":"<name>","message":"<text>"}` and
 * `job` as `{"id":"..","state":".."}`.
 *
 * Before a `result` payload is embedded it must be exactly one well-formed JSON
 * value with no trailing data, validated with the sanctioned scanner under
 * @ref k_result_scan_limits (surrounding JSON whitespace is tolerated and
 * embedded as-is). This intentionally goes beyond the "cheap sanity, not full
 * validation" wording of the subtask-04 spec: a first-character-only check
 * admits result-envelope smuggling (threat-model T-17, CWE-116) such as
 * `{"a":1},"injected":true` adding a top-level key, or `"a","injected":"true"`
 * doing the same through a string that merely starts and ends with a quote.
 * Full single-value validation closes both.
 *
 * The invariants are: exactly one of result/job/error is present; `ok == true`
 * iff the payload is a result or a job (so `error` implies `!ok`); and a result
 * payload is exactly one well-formed JSON value.
 *
 * @param response The response to serialize.
 * @return The envelope JSON text. On any invariant violation (including a
 * `result` that is not a single well-formed JSON value) the function returns a
 * well-formed `internal` failure envelope echoing `response.id` instead of
 * malformed or injected JSON (threat-model T-17).
 */
std::string serialize_response(const CommandResponse& response);

/**
 * @brief Outcome of @ref parse_response: either a parsed response or a
 * diagnostic error string.
 *
 * Exactly one side is populated: on success `response` has a value and `error`
 * is empty; on failure `response` is empty and `error` names the first rule
 * violated. The error string is a fixed, value-free diagnostic for the
 * conformance harness, not a wire `error.code`.
 */
struct parse_response_result
{
    ///< The parsed response on success; empty on failure.
    std::optional<CommandResponse> response;

    ///< Fixed diagnostic describing the first failure; empty on success.
    std::string error;
};

/**
 * @brief Parses a v1 response-envelope JSON text, the inverse of
 * @ref serialize_response.
 *
 * Exists for clients, stdio consumers and the conformance corpus (Task 07.0).
 * Uses `scan_object` for the top-level envelope and one nested `scan_object`
 * for `job`/`error`. Requires `v == 1`, a string `id`, a boolean `ok`, and
 * exactly one of `result`/`job`/`error`; `result` is captured as verbatim JSON
 * text, so an embedded quote/brace/unicode value round-trips unchanged.
 *
 * @param text The candidate response envelope JSON. Treated as untrusted.
 * @return A @ref parse_response_result. On success `response` holds the parsed
 * value and `error` is empty. On failure `response` is empty and `error`
 * describes the first problem: malformed/over-limit input, a missing or
 * ill-typed `v`/`id`/`ok`, zero or more than one payload field, an unknown
 * `error.code` name, or an unknown `job.state`.
 */
parse_response_result parse_response(std::string_view text);

/**
 * @brief HTTP status for the generic `POST /api/v1/commands` endpoint.
 *
 * @param response The response whose status to compute.
 * @return `200` when `response.ok` (including an accepted job); otherwise
 * `error_info_for(response.error.code).http_status` (the single subtask-02
 * table, e.g. `job_cancelled` is `200`).
 */
unsigned http_status_for(const CommandResponse& response);

/**
 * @brief Process exit code for the CLI `cmd` adapter.
 *
 * @param response The response whose exit code to compute.
 * @return `0` when `response.ok` (including an accepted job); otherwise
 * `error_info_for(response.error.code).exit_code` (e.g. `job_cancelled` is
 * `4`).
 */
int exit_code_for(const CommandResponse& response);

}  // namespace url_shortener::commands

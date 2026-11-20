#pragma once

/**
 * @file adapter.hpp
 * @brief Transport-adapter interface replayed by the conformance case runner.
 *
 * Every in-process transport (direct registry, TUI `InProcessClient`, ...) is
 * exercised by implementing exactly one @ref conformance::IConformanceAdapter
 * and handing it to @ref conformance::CaseRunner. The corpus is data, not code:
 * a new transport adds one adapter and reuses every corpus case unchanged. The
 * adapter is the **only** place a shim may obtain its caller role, and it must
 * do so exactly as a real caller would (`--read-only` / `CallerContext{User}`
 * for local transports) - never by relaxing a production gate or injecting a
 * role through corpus `args`/`options` (standing threat brief, ruling (c)).
 */

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace conformance
{

struct Case;
struct Step;

/**
 * @brief Identifies a corpus transport.
 *
 * The string ids (`transport_id`) are the tokens used in a case's
 * `transports.only` / `transports.skip`.
 */
enum class Transport
{
    Direct,        ///< In-process command registry (`direct`).
    RestGeneric,   ///< `POST /api/v1/commands` (`rest_generic`).
    RestResource,  ///< REST resource routes (`rest_resource`).
    CliCmd,        ///< `cmd` CLI one-shot (`cli_cmd`).
    Stdio,         ///< `serve-stdio` session (`stdio`).
    Tui            ///< TUI `InProcessClient` (`tui`).
};

/**
 * @brief Returns the corpus id string for @p transport.
 * @param transport The transport enumerator.
 * @return The lower_snake id (`"direct"`, `"rest_generic"`, ...).
 */
std::string_view transport_id(Transport transport);

/**
 * @brief Capabilities that decide which steps an adapter can carry.
 *
 * A step whose `raw_request` is set needs @ref carries_raw; a case that pins the
 * response `id` to an echoed request `id` needs @ref carries_id (otherwise the
 * server mints an `srv-<n>` id). @ref carries_options reports whether the
 * transport conveys the envelope `options` block.
 */
struct TransportCaps
{
    bool carries_id = false;       ///< Echoes the request `id` back.
    bool carries_raw = false;      ///< Can transmit verbatim `raw_request`.
    bool carries_options = false;  ///< Conveys the envelope `options` block.
};

/**
 * @brief One replayable transport under test.
 *
 * The case runner drives an adapter per case: @ref begin_case (which may decline
 * the case), one @ref send per step, then @ref end_case. Implementations own
 * their backing store/session for the duration of a case and release it in
 * @ref end_case.
 */
class IConformanceAdapter
{
public:
    virtual ~IConformanceAdapter() = default;

    /// @return The transport this adapter represents.
    virtual Transport transport() const = 0;

    /// @return The adapter's transport capabilities.
    virtual TransportCaps caps() const = 0;

    /**
     * @brief Prepares the adapter to replay @p test_case.
     *
     * @param test_case The case about to run; the adapter reads its `backend`,
     * `caller` and `files` as needed.
     * @param tmp The per-case temporary directory (already created; the runner
     * has materialized `files` into it).
     * @return `std::nullopt` when the case can run; otherwise a non-empty
     * human-readable skip reason (e.g. an unsupported backend).
     */
    virtual std::optional<std::string> begin_case(
        const Case& test_case, const std::filesystem::path& tmp) = 0;

    /**
     * @brief Sends one step's request and returns the actual response text.
     *
     * @param step The step being executed (for role overrides and metadata).
     * @param request_text The fully substituted request envelope text (or the
     * verbatim `raw_request` when the step carries raw text).
     * @return The actual response envelope as JSON text, to be parsed and
     * normalized by the runner.
     */
    virtual std::string send(const Step& step,
                             const std::string& request_text) = 0;

    /// Releases any per-case resources acquired in @ref begin_case.
    virtual void end_case() = 0;
};

}  // namespace conformance

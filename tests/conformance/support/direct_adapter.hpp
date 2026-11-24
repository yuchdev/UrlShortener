#pragma once

/**
 * @file direct_adapter.hpp
 * @brief The reference conformance adapter: replays the corpus straight into
 * the command registry with no transport in between.
 *
 * `DirectRegistryAdapter` is the fastest shim and the first run when authoring
 * a corpus case; every other transport is judged against the same expectations.
 * It parses each step's request with the 01.0 envelope parser and executes it
 * through `CommandRegistry::execute_json`, serializing the response with the
 * 01.0 serializer — it never builds a response itself, so an envelope error is
 * the registry's own `invalid_envelope` serialization, identical to every other
 * transport (subtask 04 objective).
 */

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "adapter.hpp"
#include "registry_fixture.hpp"

namespace conformance
{

/**
 * @brief In-process adapter executing corpus steps against the product
 * registry.
 *
 * Owns one @ref RegistryFixture per case (fresh store and job runner), so no
 * state leaks between cases. The `direct` transport is in-process and echo-
 * capable, so it carries the request id, verbatim `raw_request`, and the
 * envelope `options` block (@ref caps).
 */
class DirectRegistryAdapter : public IConformanceAdapter
{
public:
    /// @return @ref Transport::Direct.
    Transport transport() const override;

    /// @return `{carries_id=true, carries_raw=true, carries_options=true}`:
    /// the in-process registry echoes the id and conveys raw text and options.
    TransportCaps caps() const override;

    /**
     * @brief Builds a fresh per-case fixture over the case's backend.
     *
     * @param test_case The case about to run; its `backend` selects the store
     * and its `caller` is the default role for steps without an override.
     * @param tmp The per-case temp directory (the case runner has materialized
     * `files` into it); the `sqlite` database lives under it.
     * @return Always `std::nullopt` — the direct transport supports every
     * backend the corpus allows (`sqlite` and `inmemory`), so it declines no
     * case.
     */
    std::optional<std::string> begin_case(
        const Case& test_case, const std::filesystem::path& tmp) override;

    /**
     * @brief Executes one step's request and returns the serialized response.
     *
     * Resolves the step's effective role (`step.caller` else the case default),
     * then calls `registry.execute_json` — which runs the 01.0 envelope parser
     * and, on a parse failure, produces the shared `invalid_envelope` failure
     * response — and serializes the result with the 01.0 serializer.
     *
     * @param step The step being executed; only its `caller` override is read
     * here (the case runner already substituted and chose the request text).
     * @param request_text The fully substituted request envelope, or the
     * verbatim `raw_request` text for an envelope-error step.
     * @return The response envelope as JSON text.
     */
    std::string send(const Step& step,
                     const std::string& request_text) override;

    /// Releases the per-case fixture (stops the job runner, closes the store).
    void end_case() override;

private:
    ///< The current case's fixture; null between cases.
    std::unique_ptr<RegistryFixture> m_fixture_;

    ///< The case-level default role, applied to a step with no `caller`
    ///< override. Captured in @ref begin_case.
    std::string m_case_caller_;
};

}  // namespace conformance

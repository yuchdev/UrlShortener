#pragma once

/**
 * @file tui_adapter.hpp
 * @brief Conformance adapter replaying the corpus through the TUI client seam.
 *
 * `TuiClientAdapter` drives each corpus step through
 * `tui::InProcessClient::call`
 * - the exact `ICommandClient` seam the TUI screens submit through (ADR 0003
 * rule 1, contract C8) - so the corpus proves the TUI's command path adds no
 * behavior over the direct registry. It owns one @ref RegistryFixture per case
 * (the one product registry built by `composition::build_command_runtime`) and
 * builds an `InProcessClient` over it for the step's role, exactly as
 * `tui_entry.cpp` does: a `CallerContext` with `ControlSet::Admin` for `admin`
 * and `ControlSet::User` for `user`, never a role taken from the request body
 * (standing threat brief, ruling (c)).
 *
 * The `tui` transport is in-process and echoes the request `id` and conveys the
 * envelope `options` block, but - unlike the direct registry - it does not
 * carry verbatim `raw_request` text: the shim parses the envelope with the 01.0
 * parser before calling the client, so a malformed-envelope step is skipped by
 * capability (@ref caps mirrors Python `TRANSPORT_CAPS["tui"]`).
 *
 * C8 boundary: this header includes only the TUI client seam
 * (`in_process_client.hpp`, which is FTXUI-free and lives in
 * `url_shortener_tui_model`) and the conformance support headers; it pulls in
 * no FTXUI, no screen, and no service/repository type beyond the fixture.
 */

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <url_shortener/tui/in_process_client.hpp>

#include "adapter.hpp"
#include "registry_fixture.hpp"

namespace conformance
{

/**
 * @brief In-process adapter executing corpus steps through `InProcessClient`.
 *
 * Owns one @ref RegistryFixture per case (fresh store and job runner) and a
 * per-role `tui::InProcessClient` bound to it. The client is the only seam a
 * TUI screen uses, so a green corpus run here proves the TUI command path is
 * behavior-identical to the direct registry.
 */
class TuiClientAdapter : public IConformanceAdapter
{
public:
    /// @return @ref Transport::Tui.
    Transport transport() const override;

    /// @return `{carries_id=true, carries_raw=false, carries_options=true}`:
    /// the in-process client echoes the id and conveys `options`, but the shim
    /// parses the envelope itself and so cannot carry verbatim `raw_request`.
    TransportCaps caps() const override;

    /**
     * @brief Builds a fresh per-case fixture over the case's backend.
     *
     * @param test_case The case about to run; its `backend` selects the store
     * and its `caller` is the default role for steps without an override.
     * @param tmp The per-case temp directory (the case runner has materialized
     * `files` into it); the `sqlite` database lives under it.
     * @return Always `std::nullopt` - the TUI client supports every backend the
     * corpus allows (`sqlite` and `inmemory`), so it declines no case.
     */
    std::optional<std::string> begin_case(
        const Case& test_case, const std::filesystem::path& tmp) override;

    /**
     * @brief Parses one step's envelope, runs it through the client, and
     * returns the serialized response.
     *
     * Resolves the step's effective role (`step.caller` else the case default),
     * builds (or reuses) an `InProcessClient` for that role, parses
     * @p request_text with the 01.0 `parse_request` (the same parse the TUI's
     * command-entry path performs), calls `InProcessClient::call` - the exact
     * function the form-submit path calls - and serializes the result with the
     * 01.0 `serialize_response`. On a parse failure it mirrors
     * `CommandRegistry::execute_json`: the parser's value-free
     * `invalid_envelope` failure is returned without reaching the client (a
     * raw-text step is already skipped by @ref caps, so this is defense in
     * depth).
     *
     * @param step The step being executed; only its `caller` override is read.
     * @param request_text The fully substituted request envelope.
     * @return The response envelope as JSON text.
     */
    std::string send(const Step& step,
                     const std::string& request_text) override;

    /// Releases the per-case client and fixture (stops the job runner, closes
    /// the store).
    void end_case() override;

private:
    /**
     * @brief Returns an `InProcessClient` bound to the case registry for @p
     * role.
     *
     * Rebuilds the cached client only when the requested role differs from the
     * one it was built for, so a case whose steps mix `admin` and `user`
     * overrides gets a correctly-downgraded client per step. The caller is
     * mapped exactly as `tui_entry.cpp`: principal `"local-operator"`,
     * transport
     * `"tui"`, and `ControlSet::Admin` only for the exact role `"admin"`,
     * failing closed to `ControlSet::User` for `"user"` and every other token.
     *
     * @param role The resolved corpus role for the current step.
     * @return A reference to the cached client; valid until the next
     * @ref begin_case / @ref end_case.
     */
    url_shortener::tui::InProcessClient& client_for(const std::string& role);

    ///< The current case's fixture; null between cases. Declared before
    ///< @ref m_client_ so the client (which borrows the fixture's registry) is
    ///< destroyed first.
    std::unique_ptr<RegistryFixture> m_fixture_;

    ///< The client bound to @ref m_fixture_'s registry for @ref m_client_role_;
    ///< null until the first @ref send of a case.
    std::unique_ptr<url_shortener::tui::InProcessClient> m_client_;

    ///< The role @ref m_client_ was built for; empty when no client is cached.
    std::string m_client_role_;

    ///< The case-level default role, applied to a step with no `caller`
    ///< override. Captured in @ref begin_case.
    std::string m_case_caller_;
};

}  // namespace conformance

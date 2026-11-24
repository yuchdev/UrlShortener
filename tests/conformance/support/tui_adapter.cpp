/**
 * @file tui_adapter.cpp
 * @brief Implementation of the TUI `InProcessClient` conformance adapter.
 */

#include <utility>

#include "tui_adapter.hpp"

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/security/control_set.hpp>

#include "corpus.hpp"

namespace conformance
{

namespace commands = url_shortener::commands;

Transport TuiClientAdapter::transport() const
{
    return Transport::Tui;
}

TransportCaps TuiClientAdapter::caps() const
{
    // Mirrors Python TRANSPORT_CAPS["tui"] = Caps(id=True, raw=False,
    // options=True). The in-process client echoes the request id and the parsed
    // envelope carries `options`, but the shim parses the text itself and so
    // cannot carry a verbatim malformed `raw_request`.
    TransportCaps caps;
    caps.carries_id = true;
    caps.carries_raw = false;
    caps.carries_options = true;
    return caps;
}

std::optional<std::string> TuiClientAdapter::begin_case(
    const Case& test_case, const std::filesystem::path& tmp)
{
    m_case_caller_ = test_case.caller;
    // Drop any prior case's client and fixture before building the next: the
    // client borrows the fixture's registry, so it must die first, and two
    // fixtures must never coexist while the shared process globals
    // (LinkBackend, system.info, job runner) are reset by the new fixture's
    // constructor.
    m_client_.reset();
    m_client_role_.clear();
    m_fixture_.reset();
    m_fixture_ = std::make_unique<RegistryFixture>(tmp, test_case.backend);
    return std::nullopt;
}

url_shortener::tui::InProcessClient& TuiClientAdapter::client_for(
    const std::string& role)
{
    if (m_client_ && m_client_role_ == role) {
        return *m_client_;
    }

    // Build the caller exactly as tui_entry.cpp does: a local operator on the
    // "tui" transport whose control_set is the sole authorization input (C7).
    // Admin only for the exact role "admin"; fail closed to User for "user" and
    // every other token, so an unexpected value can never silently grant Admin
    // (standing threat brief, ruling (c)). The role is never derived from the
    // request body - the closed envelope rejects a role/caller/read_only key.
    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.transport = "tui";
    caller.control_set =
        (role == "admin") ? ControlSet::Admin : ControlSet::User;

    m_client_ = std::make_unique<url_shortener::tui::InProcessClient>(
        m_fixture_->registry(), std::move(caller));
    m_client_role_ = role;
    return *m_client_;
}

std::string TuiClientAdapter::send(const Step& step,
                                   const std::string& request_text)
{
    // The role is the corpus caller only (per-step override else the case
    // default), never derived from the request body (ruling (c)).
    const std::string role = step.caller.value_or(m_case_caller_);
    url_shortener::tui::InProcessClient& client = client_for(role);

    // Parse the envelope with the one 01.0 parser, as the TUI's command-entry
    // path does. On a parse failure, mirror CommandRegistry::execute_json: the
    // parser already resolved a safe echo id and a value-free invalid_envelope
    // error, so return that failure without reaching the client. (A raw-text
    // step is skipped by caps(), so a well-formed corpus envelope normally
    // parses here; this branch is defense in depth.)
    commands::parse_request_result parsed =
        commands::parse_request(request_text);
    commands::CommandResponse response;
    if (!parsed.ok()) {
        response = commands::make_failure(std::move(parsed.id),
                                          std::move(parsed.error));
    }
    else {
        // The exact seam the form-submit path calls: no adapter-built response.
        response = client.call(*parsed.request);
    }
    return commands::serialize_response(response);
}

void TuiClientAdapter::end_case()
{
    m_client_.reset();
    m_client_role_.clear();
    m_fixture_.reset();
}

}  // namespace conformance

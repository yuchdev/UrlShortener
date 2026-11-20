/**
 * @file direct_adapter.cpp
 * @brief Implementation of the direct registry conformance adapter.
 */

#include "direct_adapter.hpp"

#include "corpus.hpp"
#include "url_shortener/commands/command_registry.hpp"
#include "url_shortener/commands/envelope.hpp"

namespace conformance
{

Transport DirectRegistryAdapter::transport() const
{
    return Transport::Direct;
}

TransportCaps DirectRegistryAdapter::caps() const
{
    TransportCaps caps;
    caps.carries_id = true;
    caps.carries_raw = true;
    caps.carries_options = true;
    return caps;
}

std::optional<std::string> DirectRegistryAdapter::begin_case(
    const Case& test_case, const std::filesystem::path& tmp)
{
    m_case_caller_ = test_case.caller;
    m_fixture_ = std::make_unique<RegistryFixture>(tmp, test_case.backend);
    return std::nullopt;
}

std::string DirectRegistryAdapter::send(const Step& step,
                                        const std::string& request_text)
{
    // The role is the corpus caller only (per-step override else the case
    // default), never derived from the request body (standing threat brief,
    // ruling (c)).
    const std::string role = step.caller.value_or(m_case_caller_);
    const url_shortener::commands::CallerContext context =
        m_fixture_->caller(role);

    // execute_json runs the 01.0 envelope parser and, on a parse failure,
    // yields the shared `invalid_envelope` failure response; the adapter never
    // builds a response itself.
    const url_shortener::commands::CommandResponse response =
        m_fixture_->registry().execute_json(context, request_text);
    return url_shortener::commands::serialize_response(response);
}

void DirectRegistryAdapter::end_case()
{
    m_fixture_.reset();
}

}  // namespace conformance

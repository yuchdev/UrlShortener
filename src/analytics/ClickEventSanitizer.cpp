#include "url_shortener/analytics/ClickEventSanitizer.hpp"

#include <algorithm>
#include <cctype>

namespace url_shortener::analytics {

std::string ClickEventSanitizer::SanitizeReferrer(const std::string& input, const SanitizationLimits& limits) { return SanitizeCommon(input, limits.max_referrer_bytes); }
std::string ClickEventSanitizer::SanitizeUserAgent(const std::string& input, const SanitizationLimits& limits) { return SanitizeCommon(input, limits.max_user_agent_bytes); }
std::string ClickEventSanitizer::SanitizeDomain(const std::string& input, const SanitizationLimits& limits) { return SanitizeCommon(input, limits.max_domain_bytes); }

bool ClickEventSanitizer::ValidateLimits(const SanitizationLimits& limits)
{
    return limits.max_referrer_bytes > 0 && limits.max_user_agent_bytes > 0 && limits.max_domain_bytes > 0;
}

std::string ClickEventSanitizer::SanitizeCommon(const std::string& input, std::size_t max_bytes)
{
    std::string filtered;
    filtered.reserve(input.size());
    for (unsigned char ch : input) {
        if (ch >= 0x20 && ch != 0x7F) {
            filtered.push_back(static_cast<char>(ch));
        }
    }

    auto first = std::find_if_not(filtered.begin(), filtered.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    auto last = std::find_if_not(filtered.rbegin(), filtered.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    std::string trimmed = (first < last) ? std::string(first, last) : std::string();

    if (trimmed.size() > max_bytes) {
        trimmed.resize(max_bytes);
    }
    return trimmed;
}

} // namespace url_shortener::analytics

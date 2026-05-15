#pragma once

#include <cstddef>
#include <string>

namespace url_shortener::analytics {

/**
 * @brief Byte limits and behavior for metadata sanitization.
 */
struct SanitizationLimits {
    std::size_t max_referrer_bytes = 512;
    std::size_t max_user_agent_bytes = 512;
    std::size_t max_domain_bytes = 255;
};

/**
 * @brief Sanitizes metadata fields by removing control bytes, trimming whitespace, and truncating.
 */
class ClickEventSanitizer {
public:
    static std::string SanitizeReferrer(const std::string& input, const SanitizationLimits& limits);
    static std::string SanitizeUserAgent(const std::string& input, const SanitizationLimits& limits);
    static std::string SanitizeDomain(const std::string& input, const SanitizationLimits& limits);
    static bool ValidateLimits(const SanitizationLimits& limits);

private:
    static std::string SanitizeCommon(const std::string& input, std::size_t max_bytes);
};

} // namespace url_shortener::analytics

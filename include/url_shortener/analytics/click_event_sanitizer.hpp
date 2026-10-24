#pragma once

#include <cstddef>
#include <string>

namespace url_shortener::analytics
{

/**
 * @brief Byte limits and behavior for metadata sanitization. All three
 * limits must be > 0 per `ValidateLimits`.
 */
struct SanitizationLimits
{
    ///< Maximum length, in bytes, of a sanitized `Referer` value;
    ///< longer input is truncated by
    ///< `ClickEventSanitizer::SanitizeReferrer`.
    std::size_t max_referrer_bytes = 512;

    ///< Maximum length, in bytes, of a sanitized `User-Agent` value;
    ///< longer input is truncated by
    ///< `ClickEventSanitizer::SanitizeUserAgent`.
    std::size_t max_user_agent_bytes = 512;
    
    ///< Maximum length, in bytes, of a sanitized domain value; longer
    ///< input is truncated by `ClickEventSanitizer::SanitizeDomain`.
    std::size_t max_domain_bytes = 255;
};

/**
 * @brief Sanitizes metadata fields by removing control bytes, trimming
 * whitespace, and truncating to a configured byte limit.
 */
class ClickEventSanitizer
{
public:
    /**
     * @brief Sanitizes a raw `Referer` header value.
     * @param input Untrusted raw value.
     * @param limits Supplies `max_referrer_bytes`.
     * @return `input` with ASCII control bytes (`< 0x20` or `0x7F`)
     * removed, leading/trailing whitespace trimmed, then truncated to
     * `limits.max_referrer_bytes` bytes. Truncation is byte-based, not
     * UTF-8-aware, so it can cut a multi-byte character in half.
     */
    static std::string SanitizeReferrer(const std::string& input,
                                        const SanitizationLimits& limits);

    /**
     * @brief Sanitizes a raw `User-Agent` header value.
     * @param input Untrusted raw value.
     * @param limits Supplies `max_user_agent_bytes`.
     * @return `input` with ASCII control bytes removed, whitespace
     * trimmed, then truncated to `limits.max_user_agent_bytes` bytes (see
     * the UTF-8 caveat on `SanitizeReferrer`).
     */
    static std::string SanitizeUserAgent(const std::string& input,
                                         const SanitizationLimits& limits);

    /**
     * @brief Sanitizes a raw `Host`/domain value.
     * @param input Untrusted raw value.
     * @param limits Supplies `max_domain_bytes`.
     * @return `input` with ASCII control bytes removed, whitespace
     * trimmed, then truncated to `limits.max_domain_bytes` bytes (see the
     * UTF-8 caveat on `SanitizeReferrer`).
     */
    static std::string SanitizeDomain(const std::string& input,
                                      const SanitizationLimits& limits);

    /**
     * @brief Checks that `limits` is usable (no zero-length limit that
     * would truncate every value to empty).
     * @param limits Limits to check.
     * @return `true` if `max_referrer_bytes`, `max_user_agent_bytes`, and
     * `max_domain_bytes` are all > 0; `false` otherwise.
     */
    static bool ValidateLimits(const SanitizationLimits& limits);

private:
    /**
     * @brief Shared implementation for the `Sanitize*` methods: strips
     * ASCII control bytes (`< 0x20` or `0x7F`), trims leading/trailing
     * whitespace, then truncates to `max_bytes`.
     * @param input Untrusted raw value.
     * @param max_bytes Maximum output length in bytes.
     * @return The sanitized, possibly-truncated string.
     */
    static std::string SanitizeCommon(const std::string& input,
                                      std::size_t max_bytes);
};

}  // namespace url_shortener::analytics

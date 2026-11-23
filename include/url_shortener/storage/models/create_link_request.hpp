#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <regex>
#include <string>

/**
 * @brief Input model for `IMetadataRepository::CreateLink`; carries
 * all caller-supplied fields needed to construct a new `LinkRecord`.
 * Backends validate via `isValid()` before persisting; callers should
 * not invoke `CreateLink` with an invalid request.
 */
struct CreateLinkRequest
{
    ///< Public short-code slug for the link; must match
    ///< `^[A-Za-z0-9_-]{3,64}$` (enforced by `isValid()`).
    std::string short_code;

    ///< Destination URL; must be non-empty (enforced by `isValid()`).
    ///< Backends do not re-validate the URL scheme.
    std::string target_url;

    ///< Optional wall-clock expiry; absent means the link does not
    ///< expire.
    std::optional<std::chrono::system_clock::time_point> expires_at;

    ///< Optional owner identifier for per-owner listing and access
    ///< control; absent means the link has no owner.
    std::optional<std::string> owner;

    ///< Arbitrary key-value metadata attached to the link (e.g.
    ///< tags, campaign fields); may be empty.
    std::map<std::string, std::string> attributes;

    ///< Initial enabled state for the link; defaults to `true`.
    bool is_active = true;

    /**
     * @brief Validates request-level constraints shared by all
     * backends, without touching any backend state.
     * @return `true` if `target_url` is non-empty and `short_code`
     * matches `^[A-Za-z0-9_-]{3,64}$`; `false` otherwise.
     */
    bool isValid() const
    {
        static const std::regex short_code_regex("^[A-Za-z0-9_-]{3,64}$");
        return !target_url.empty()
            && std::regex_match(short_code, short_code_regex);
    }
};

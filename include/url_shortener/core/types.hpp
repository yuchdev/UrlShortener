#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace url_shortener
{

/**
 * @brief HTTP redirect semantics for a short link.
 */
enum class RedirectType
{
    ///< HTTP 302 Found; clients are not expected to cache the
    ///< redirect target. Use for links whose targets may change.
    temporary,

    ///< HTTP 301 Moved Permanently; browsers and crawlers may
    ///< cache the redirect indefinitely. Use only for stable links.
    permanent
};

/**
 * @brief Stream formatter for `RedirectType`.
 * @param os Stream to write to.
 * @param value Value to format.
 * @return `os` with `"temporary"` or `"permanent"` written.
 */
inline std::ostream& operator<<(std::ostream& os, const RedirectType value)
{
    switch (value) {
        case RedirectType::permanent:
            os << "permanent";
            break;
        case RedirectType::temporary:
            os << "temporary";
            break;
        default:
            os << "unknown";
            break;
    }
    return os;
}

/**
 * @brief Computed lifecycle status of a link, derived by
 * `resolveLinkStatus` from `deleted_at`, `enabled`, and `expires_at`.
 *
 * Priority order (highest wins): `deleted` > `disabled` >
 * `expired` > `active`.
 */
enum class LinkStatus
{
    ///< Link is live and will serve redirects.
    active,

    ///< Link exists but `enabled == false`; serves no redirects
    ///< until re-enabled.
    disabled,

    ///< Link's `expires_at` timestamp is in the past; serves no
    ///< redirects.
    expired,

    ///< Link has been soft-deleted (`deleted_at` is set); serves
    ///< no redirects and is excluded from normal listings.
    deleted
};

/**
 * @brief Stream formatter for `LinkStatus`.
 * @param os Stream to write to.
 * @param value Value to format.
 * @return `os` with the status name (`"active"`, `"disabled"`,
 * `"expired"`, or `"deleted"`) written.
 */
inline std::ostream& operator<<(std::ostream& os, const LinkStatus value)
{
    switch (value) {
        case LinkStatus::deleted:
            os << "deleted";
            break;
        case LinkStatus::disabled:
            os << "disabled";
            break;
        case LinkStatus::expired:
            os << "expired";
            break;
        case LinkStatus::active:
            os << "active";
            break;
        default:
            os << "unknown";
            break;
    }
    return os;
}

/**
 * @brief Error categories for metadata repository operations.
 *
 * Written to the `RepoError*` out-param on failure by
 * `IMetadataRepository` implementations, and forwarded through
 * `LinkService::Update` and `LinkService::Delete`.
 */
enum class RepoError
{
    ///< The requested record does not exist in the repository.
    not_found,

    ///< A record with the same key (slug or ID) already exists;
    ///< the create operation was not performed.
    already_exists,

    ///< A recoverable backend error (e.g. a transient network
    ///< timeout); the caller may retry.
    transient_failure,

    ///< A non-recoverable backend error; retrying is unlikely to
    ///< succeed.
    permanent_failure
};

/**
 * @brief Stream formatter for `RepoError`.
 * @param os Stream to write to.
 * @param value Value to format.
 * @return `os` with the error name (e.g. `"not_found"`) written.
 */
inline std::ostream& operator<<(std::ostream& os, const RepoError value)
{
    switch (value) {
        case RepoError::not_found:
            os << "not_found";
            break;
        case RepoError::already_exists:
            os << "already_exists";
            break;
        case RepoError::transient_failure:
            os << "transient_failure";
            break;
        case RepoError::permanent_failure:
            os << "permanent_failure";
            break;
        default:
            os << "unknown";
            break;
    }
    return os;
}

/**
 * @brief Core link record as stored in the metadata repository.
 *
 * All timestamp fields use ISO 8601 / RFC3339 UTC format
 * (e.g. `"2025-01-01T00:00:00Z"`). The `stats` sub-struct is
 * maintained by the repository and is read-only from the
 * application layer.
 */
struct Link
{
    /**
     * @brief Optional UTM-style campaign attribution for a link.
     *
     * All fields are optional; each is capped at 128 characters
     * by `validateCampaign`.
     */
    struct Campaign
    {
        ///< Campaign name (UTM `utm_campaign`).
        std::optional<std::string> name;

        ///< Traffic source (UTM `utm_source`).
        std::optional<std::string> source;

        ///< Marketing medium (UTM `utm_medium`).
        std::optional<std::string> medium;

        ///< Paid search term (UTM `utm_term`).
        std::optional<std::string> term;

        ///< Ad content differentiator (UTM `utm_content`).
        std::optional<std::string> content;

        ///< Campaign identifier.
        std::optional<std::string> id;
    };

    /**
     * @brief Redirect counters maintained by the repository;
     * populated on reads, ignored on writes.
     */
    struct Stats
    {
        ///< Lifetime redirect count.
        uint64_t total_redirects = 0;

        ///< Rolling 24-hour redirect count.
        uint64_t redirects_24h = 0;

        ///< Rolling 7-day redirect count.
        uint64_t redirects_7d = 0;

        ///< ISO 8601 timestamp of the most recent redirect; absent if
        ///< the link has never been redirected.
        std::optional<std::string> last_accessed_at;
    };

    ///< Stable opaque link identifier (UUID assigned at creation).
    std::string id;

    ///< Public short-code slug used in redirect URLs.
    std::string slug;

    ///< Normalized destination URL (absolute HTTP/HTTPS).
    std::string target_url;

    ///< ISO 8601 / RFC3339 UTC creation timestamp.
    std::string created_at;

    ///< ISO 8601 / RFC3339 UTC timestamp of the last mutation.
    std::string updated_at;

    ///< Optional ISO 8601 expiry timestamp; absent means the link
    ///< never expires.
    std::optional<std::string> expires_at;

    ///< Optional ISO 8601 soft-delete timestamp; absent means the
    ///< link is not deleted.
    std::optional<std::string> deleted_at;

    ///< `false` disables redirects without deleting the record.
    bool enabled = true;

    ///< User-supplied tag list (max 20 tags, max 32 chars each).
    std::vector<std::string> tags;

    ///< User-supplied metadata (max 50 keys; keys max 64 chars,
    ///< values max 512 chars).
    std::unordered_map<std::string, std::string> metadata;

    ///< Optional UTM campaign attribution.
    std::optional<Campaign> campaign;

    ///< Lifetime and rolling-window redirect counters.
    Stats stats;

    ///< HTTP redirect semantics (302 or 301).
    RedirectType redirect_type = RedirectType::temporary;
};

/**
 * @brief Click-event record capturing a single redirect attempt for
 * analytics storage. Fields are pre-sanitized by the analytics
 * pipeline before this struct is constructed.
 */
struct ClickEvent
{
    ///< Opaque event identifier; caller-assigned.
    std::string event_id;

    ///< ISO 8601 / RFC3339 UTC timestamp of the redirect attempt.
    std::string occurred_at;

    ///< Short-code slug that was resolved.
    std::string slug;

    ///< Stable identifier of the resolved link; absent if the slug
    ///< was not found.
    std::optional<std::string> link_id;

    ///< `Host` header from the inbound request, sanitized and
    ///< truncated.
    std::string domain;

    ///< HTTP status code returned by the redirect response.
    uint16_t status_code = 0;

    ///< Sanitized `Referer` header value; absent if not present.
    std::optional<std::string> referrer;

    ///< Sanitized `User-Agent` header value; absent if not present.
    std::optional<std::string> user_agent;

    ///< HMAC-SHA256 hash of the client identifier (see
    ///< `hashClientIdentifier`); the raw identifier is never stored.
    std::string client_id_hash;
};

}  // namespace url_shortener

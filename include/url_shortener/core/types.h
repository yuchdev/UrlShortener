#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <cstdint>
#include <ostream>

namespace url_shortener {

enum class RedirectType
{
    temporary,
    permanent
};

inline std::ostream& operator<<(std::ostream& os, const RedirectType value)
{
    switch (value) {
    case RedirectType::permanent: os << "permanent"; break;
    case RedirectType::temporary: os << "temporary"; break;
    default: os << "unknown"; break;
    }
    return os;
}

enum class LinkStatus
{
    active,
    disabled,
    expired,
    deleted
};

inline std::ostream& operator<<(std::ostream& os, const LinkStatus value)
{
    switch (value) {
    case LinkStatus::deleted: os << "deleted"; break;
    case LinkStatus::disabled: os << "disabled"; break;
    case LinkStatus::expired: os << "expired"; break;
    case LinkStatus::active: os << "active"; break;
    default: os << "unknown"; break;
    }
    return os;
}

enum class RepoError
{
    not_found,
    already_exists,
    transient_failure,
    permanent_failure
};

inline std::ostream& operator<<(std::ostream& os, const RepoError value)
{
    switch (value) {
    case RepoError::not_found: os << "not_found"; break;
    case RepoError::already_exists: os << "already_exists"; break;
    case RepoError::transient_failure: os << "transient_failure"; break;
    case RepoError::permanent_failure: os << "permanent_failure"; break;
    default: os << "unknown"; break;
    }
    return os;
}

struct Link
{
    struct Campaign
    {
        std::optional<std::string> name;  ///< Campaign name.
        std::optional<std::string> source;  ///< Campaign source.
        std::optional<std::string> medium;  ///< Campaign medium.
        std::optional<std::string> term;  ///< Campaign term.
        std::optional<std::string> content;  ///< Campaign content.
        std::optional<std::string> id;  ///< Campaign identifier.
    };

    struct Stats
    {
        uint64_t total_redirects = 0;  ///< Lifetime redirect count.
        uint64_t redirects_24h = 0;  ///< Rolling 24h redirect count.
        uint64_t redirects_7d = 0;  ///< Rolling 7d redirect count.
        std::optional<std::string> last_accessed_at;  ///< Last redirect timestamp.
    };

    std::string id;  ///< Stable link identifier.
    std::string slug;  ///< Public redirect slug.
    std::string target_url;  ///< Normalized destination URL.
    std::string created_at;  ///< Creation timestamp.
    std::string updated_at;  ///< Last update timestamp.
    std::optional<std::string> expires_at;  ///< Optional expiry timestamp.
    std::optional<std::string> deleted_at;  ///< Optional soft-delete timestamp.
    bool enabled = true;  ///< Enable/disable switch.
    std::vector<std::string> tags;  ///< Optional link tags.
    std::unordered_map<std::string, std::string> metadata;  ///< Optional metadata map.
    std::optional<Campaign> campaign;  ///< Optional campaign metadata.
    Stats stats;  ///< Redirect counters and timestamps.
    RedirectType redirect_type = RedirectType::temporary;  ///< Redirect semantics.
};

struct ClickEvent
{
    std::string event_id;  ///< Event identifier.
    std::string occurred_at;  ///< Event timestamp.
    std::string slug;  ///< Redirect slug associated with event.
    std::optional<std::string> link_id;  ///< Optional resolved link id.
    std::string domain;  ///< Request host domain.
    uint16_t status_code = 0;  ///< HTTP response status code.
    std::optional<std::string> referrer;  ///< Optional Referer header value.
    std::optional<std::string> user_agent;  ///< Optional User-Agent header value.
    std::string client_id_hash;  ///< Anonymized client hash.
};

} // namespace url_shortener

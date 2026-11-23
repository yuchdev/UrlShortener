#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

/**
 * @brief Canonical persisted short-link metadata record, returned by
 * `IMetadataRepository` read operations and passed into write
 * operations such as `UpdateLink`.
 */
struct LinkRecord
{
    ///< Stable unique identifier assigned at creation by the caller;
    ///< never changed after the record is first written.
    std::string id;

    ///< Public short-code slug; must match
    ///< `^[A-Za-z0-9_-]{3,64}$` per `CreateLinkRequest::isValid()`.
    std::string short_code;

    ///< Destination URL to redirect to.
    std::string target_url;

    ///< Wall-clock timestamp when the record was created (the `now`
    ///< argument passed to `IMetadataRepository::CreateLink`).
    std::chrono::system_clock::time_point created_at;

    ///< Wall-clock timestamp of the most recent mutation.
    std::chrono::system_clock::time_point updated_at;

    ///< Optional wall-clock expiry; absent means the link does not
    ///< expire.
    std::optional<std::chrono::system_clock::time_point> expires_at;

    ///< Soft-delete timestamp set by `DeleteLink`; absent means the
    ///< link has not been soft-deleted.
    std::optional<std::chrono::system_clock::time_point> deleted_at;

    ///< Optional owner identifier; absent means no owner is
    ///< associated.
    std::optional<std::string> owner;

    ///< Arbitrary key-value metadata; may be empty.
    std::map<std::string, std::string> attributes;

    ///< `true` when the link is enabled and may serve redirects.
    bool is_active = true;

    ///< Optimistic-concurrency counter; starts at 1 on creation and
    ///< incremented by some backends on each `UpdateLink` call.
    uint64_t version = 1;
};

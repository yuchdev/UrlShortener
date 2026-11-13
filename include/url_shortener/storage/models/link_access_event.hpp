#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

/**
 * @brief Analytics payload emitted for each redirect-resolution
 * attempt, passed to `IAnalyticsSink::Emit`. The sink must not
 * retain a reference to the event beyond the `Emit` call.
 */
struct LinkAccessEvent
{
    ///< Short-code slug of the link that was resolved.
    std::string short_code;

    ///< Wall-clock timestamp when the redirect attempt completed.
    std::chrono::system_clock::time_point occurred_at;

    ///< HTTP response status code sent to the client (e.g. 301,
    ///< 302, 404).
    uint16_t status_code = 0;

    ///< `true` when the lookup was served from `ICacheStore` without
    ///< hitting `IMetadataRepository`.
    bool cache_hit = false;

    ///< Optional client identifier; absent when the caller does not
    ///< supply one (the current `LinkService` always passes
    ///< `std::nullopt`).
    std::optional<std::string> client_id;
};

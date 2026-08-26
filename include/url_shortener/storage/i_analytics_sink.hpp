#pragma once

#include "url_shortener/storage/errors/analytics_error.hpp"
#include "url_shortener/storage/models/link_access_event.hpp"

/**
 * @brief Best-effort analytics sink abstraction.
 */
class IAnalyticsSink {
public:
    virtual ~IAnalyticsSink() = default;

    /**
     * @brief Emits one access event.
     */
    virtual bool Emit(const LinkAccessEvent& event, AnalyticsError* error = nullptr) = 0;

    /**
     * @brief Flushes buffered analytics state.
     */
    virtual bool Flush(AnalyticsError* error = nullptr) = 0;
};

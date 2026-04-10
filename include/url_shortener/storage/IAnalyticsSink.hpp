#pragma once

#include "url_shortener/storage/errors/AnalyticsError.hpp"
#include "url_shortener/storage/models/LinkAccessEvent.hpp"

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

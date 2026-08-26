#pragma once

#include <mutex>
#include <vector>

#include "url_shortener/storage/i_analytics_sink.hpp"

/**
 * @brief In-memory analytics sink for deterministic tests.
 */
class InMemoryAnalyticsSink final : public IAnalyticsSink {
public:
    bool Emit(const LinkAccessEvent& event, AnalyticsError* error = nullptr) override;
    bool Flush(AnalyticsError* error = nullptr) override;
    std::vector<LinkAccessEvent> Snapshot() const;

private:
    mutable std::mutex mutex_;
    std::vector<LinkAccessEvent> events_;
};

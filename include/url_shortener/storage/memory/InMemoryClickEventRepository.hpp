#pragma once

#include <mutex>
#include <vector>

#include "url_shortener/analytics/IClickEventRepository.hpp"

namespace url_shortener::storage::memory {

/**
 * @brief In-memory click-event repository for tests and local development.
 */
class InMemoryClickEventRepository final : public analytics::IClickEventRepository {
public:
    bool InsertBatch(const std::vector<analytics::ClickEvent>& events, analytics::AnalyticsError* error = nullptr) override;
    bool GetAggregateStats(const analytics::AggregateQuery& query, analytics::AggregateStats* stats, analytics::AnalyticsError* error = nullptr) override;
    bool DeleteOlderThan(analytics::Timestamp cutoff, analytics::AnalyticsError* error = nullptr) override;

private:
    std::vector<analytics::ClickEvent> events_;
    std::mutex mu_;
};

}

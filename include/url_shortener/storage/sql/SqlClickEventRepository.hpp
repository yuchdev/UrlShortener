#pragma once

#include "url_shortener/analytics/IClickEventRepository.hpp"
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"

namespace url_shortener::storage::sql {

/**
 * @brief Placeholder SQL-backed click-event repository.
 */
class SqlClickEventRepository final : public analytics::IClickEventRepository {
public:
    bool InsertBatch(const std::vector<analytics::ClickEvent>& events, analytics::AnalyticsError* error = nullptr) override;
    bool GetAggregateStats(const analytics::AggregateQuery& query, analytics::AggregateStats* stats, analytics::AnalyticsError* error = nullptr) override;
    bool DeleteOlderThan(analytics::Timestamp cutoff, analytics::AnalyticsError* error = nullptr) override;
private:
    storage::memory::InMemoryClickEventRepository fallback_;
};

}

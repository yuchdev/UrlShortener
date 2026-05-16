#include "url_shortener/storage/sql/SqlClickEventRepository.hpp"

namespace url_shortener::storage::sql {

bool SqlClickEventRepository::InsertBatch(const std::vector<analytics::ClickEvent>& events, analytics::AnalyticsError* error) {
    return fallback_.InsertBatch(events, error);
}
bool SqlClickEventRepository::GetAggregateStats(const analytics::AggregateQuery& query, analytics::AggregateStats* stats, analytics::AnalyticsError* error) {
    return fallback_.GetAggregateStats(query, stats, error);
}
bool SqlClickEventRepository::DeleteOlderThan(analytics::Timestamp cutoff, analytics::AnalyticsError* error) {
    return fallback_.DeleteOlderThan(cutoff, error);
}

}

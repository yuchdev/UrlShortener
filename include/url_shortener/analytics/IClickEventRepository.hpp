#pragma once

#include <chrono>
#include <vector>

#include "url_shortener/analytics/AggregateQuery.hpp"
#include "url_shortener/analytics/AggregateStats.hpp"
#include "url_shortener/analytics/ClickEvent.hpp"

namespace url_shortener::analytics {

class IClickEventRepository {
public:
    virtual ~IClickEventRepository() = default;
    virtual bool InsertBatch(const std::vector<ClickEvent>& events, AnalyticsError* error = nullptr) = 0;
    virtual bool GetAggregateStats(const AggregateQuery& query, AggregateStats* stats, AnalyticsError* error = nullptr) = 0;
    virtual bool DeleteOlderThan(Timestamp cutoff, AnalyticsError* error = nullptr) = 0;
};

} // namespace

#pragma once

#include <chrono>
#include <optional>
#include <ostream>
#include <string>

#include "url_shortener/analytics/AnalyticsError.hpp"

namespace url_shortener::analytics {

/**
 * @brief Aggregate bucket granularity.
 */
enum class AggregateBucket { hour, day, week };

/**
 * @brief Stream formatter for aggregate bucket values.
 */
inline std::ostream& operator<<(std::ostream& os, AggregateBucket bucket)
{
    switch (bucket) {
    case AggregateBucket::hour: return os << "AggregateBucket::hour";
    case AggregateBucket::day:  return os << "AggregateBucket::day";
    case AggregateBucket::week: return os << "AggregateBucket::week";
    }
    return os << "AggregateBucket(" << static_cast<int>(bucket) << ")";
}

/**
 * @brief Aggregate query DTO.
 */
struct AggregateQuery {
    std::optional<std::string> slug;
    std::optional<std::string> link_id;
    std::chrono::system_clock::time_point from{};
    std::chrono::system_clock::time_point to{};
    std::optional<AggregateBucket> bucket;
};

/**
 * @brief Validator for aggregate query parameters.
 */
class AggregateQueryValidator {
public:
    static bool Validate(AggregateQuery* query, AnalyticsError* error = nullptr);
};

} // namespace url_shortener::analytics

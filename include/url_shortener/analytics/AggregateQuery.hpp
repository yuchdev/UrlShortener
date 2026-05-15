#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/analytics/AnalyticsError.hpp"

namespace url_shortener::analytics {

/**
 * @brief Aggregate bucket granularity.
 */
enum class AggregateBucket { hour, day, week };

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

} // namespace

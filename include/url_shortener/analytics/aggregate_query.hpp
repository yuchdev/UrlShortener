#pragma once

#include <chrono>
#include <optional>
#include <ostream>
#include <string>

#include "url_shortener/analytics/analytics_error.hpp"

namespace url_shortener::analytics
{

/**
 * @brief Time-bucket granularity for aggregate analytics queries.
 */
enum class AggregateBucket
{
    ///< Group results into one-hour buckets.
    hour,

    ///< Group results into one-day buckets; the default applied by
    ///< `AggregateQueryValidator::Validate` when a query omits `bucket`.
    day,

    ///< Group results into one-week buckets.      
    week
};

/**
 * @brief Stream formatter for aggregate bucket values.
 * @param os Stream to write to.
 * @param bucket Value to format.
 * @return `os`, with the bucket's symbolic name (e.g. `"AggregateBucket::day"`)
 * written to it.
 */
inline std::ostream& operator<<(std::ostream& os, AggregateBucket bucket)
{
    switch (bucket) {
        case AggregateBucket::hour:
            return os << "AggregateBucket::hour";
        case AggregateBucket::day:
            return os << "AggregateBucket::day";
        case AggregateBucket::week:
            return os << "AggregateBucket::week";
    }
    return os << "AggregateBucket(" << static_cast<int>(bucket) << ")";
}

/**
 * @brief Filter/time-range parameters for an aggregate analytics query.
 *
 * `from`/`to` define a half-open range: implementations include events with
 * `from <= occurred_at < to`. Pass this to `AggregateQueryValidator::Validate`
 * before handing it to `IClickEventRepository::GetAggregateStats`.
 */
struct AggregateQuery
{
    ///< Restrict results to this slug; unset means no slug filter.
    ///< May be combined with `link_id`.
    std::optional<std::string> slug;

    ///< Restrict results to this link id; unset means no link-id
    ///< filter. May be combined with `slug`.
    std::optional<std::string> link_id;

    ///< Inclusive start of the time range.              
    std::chrono::system_clock::time_point from {};

    ///< Exclusive end of the time range.
    std::chrono::system_clock::time_point to {};

    ///< Time-bucket granularity for
    ///< `AggregateStats::time_buckets`; defaulted to
    ///< `AggregateBucket::day` by
    ///< `AggregateQueryValidator::Validate` if left unset.
    std::optional<AggregateBucket> bucket;
};

/**
 * @brief Validates and normalizes aggregate query parameters.
 */
class AggregateQueryValidator
{
public:

    /**
     * @brief Validates `query` and fills in its default bucket.
     * @param query Query to validate, mutated in place: if `query->bucket`
     * is unset, it is set to `AggregateBucket::day` even when validation
     * otherwise fails. The caller should discard `query` on a `false`
     * return rather than relying on any other field being normalized.
     * @param[out] error If non-null and validation fails, receives
     * `AnalyticsErrorCode::validation` with a message describing which
     * check failed; left untouched on success.
     * @return `true` if `query` is non-null, `query->from <= query->to`, the
     * range spans at most 366 days, and `bucket` (after defaulting) is one
     * of the known `AggregateBucket` values; `false` otherwise, including
     * when `query` is `nullptr`.
     */
    static bool Validate(AggregateQuery* query, AnalyticsError* error = nullptr);
};

}  // namespace url_shortener::analytics

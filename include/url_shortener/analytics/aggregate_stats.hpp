#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace url_shortener::analytics
{

/** @brief Number of matching click events that had a given HTTP status code. */
struct StatusCodeCount
{
    ///< HTTP status code the redirect responded with.
    uint16_t status_code = 0;  

    ///< Number of matching events with this status code.
    std::uint64_t count = 0;
};

/** @brief Number of matching click events observed for a given domain. */
struct DomainCount
{
    ///< Sanitized `Host` header value (see `ClickEventSanitizer::SanitizeDomain`).
    std::string domain;

    ///< Number of matching events for this domain.
    std::uint64_t count = 0;
};

/** @brief Number of matching click events falling into one time bucket. */
struct TimeBucketCount
{
    ///< Start of the bucket, aligned to the query's `AggregateBucket` granularity.
    std::chrono::system_clock::time_point bucket_start {};

    ///< Number of matching events whose `occurred_at` falls in this bucket.
    std::uint64_t count = 0;
};

/**
 * @brief Aggregate statistics response model for an `AggregateQuery`.
 *
 * Produced by `IClickEventRepository::GetAggregateStats`; all counts are
 * scoped to the events matched by the query's filters and time range.
 */
struct AggregateStats
{
    ///< Total number of matching click events, regardless of status code.
    std::uint64_t total_attempts = 0;

    ///< Subset of `total_attempts` whose status code is a 3xx redirect (300-399).
    std::uint64_t successful_redirects = 0;

    ///< Breakdown of `total_attempts` by exact status code.
    std::vector<StatusCodeCount> status_code_counts;

    ///< Breakdown of `total_attempts` by domain.
    std::vector<DomainCount> domain_counts;

    ///< Breakdown of `total_attempts` by time bucket, sorted
    ///< ascending by `bucket_start`; empty if the query's `bucket` was unset.
    std::vector<TimeBucketCount> time_buckets;                       
};

}  // namespace url_shortener::analytics

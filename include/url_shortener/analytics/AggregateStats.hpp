#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace url_shortener::analytics {

struct StatusCodeCount { uint16_t status_code = 0; std::uint64_t count = 0; };
struct DomainCount { std::string domain; std::uint64_t count = 0; };
struct TimeBucketCount { std::chrono::system_clock::time_point bucket_start{}; std::uint64_t count = 0; };

/**
 * @brief Aggregate statistics response model.
 */
struct AggregateStats {
    std::uint64_t total_attempts = 0;
    std::uint64_t successful_redirects = 0;
    std::vector<StatusCodeCount> status_code_counts;
    std::vector<DomainCount> domain_counts;
    std::vector<TimeBucketCount> time_buckets;
};

} // namespace

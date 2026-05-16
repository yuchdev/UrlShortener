#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"

#include <algorithm>
#include <unordered_map>

namespace url_shortener::storage::memory {
namespace {
using namespace analytics;
std::chrono::system_clock::time_point bucket_start(std::chrono::system_clock::time_point tp, AggregateBucket b) {
    using namespace std::chrono;
    auto s = duration_cast<seconds>(tp.time_since_epoch()).count();
    long long width = b == AggregateBucket::hour ? 3600LL : (b == AggregateBucket::day ? 86400LL : 604800LL);
    return std::chrono::system_clock::time_point{seconds((s / width) * width)};
}
}

bool InMemoryClickEventRepository::InsertBatch(const std::vector<analytics::ClickEvent>& events, analytics::AnalyticsError*) {
    std::scoped_lock lk(mu_);
    events_.insert(events_.end(), events.begin(), events.end());
    return true;
}

bool InMemoryClickEventRepository::GetAggregateStats(const analytics::AggregateQuery& query, analytics::AggregateStats* stats, analytics::AnalyticsError*) {
    if (!stats) return false;
    std::scoped_lock lk(mu_);
    *stats = {};
    std::unordered_map<int, std::uint64_t> by_status;
    std::unordered_map<std::string, std::uint64_t> by_domain;
    std::unordered_map<long long, std::uint64_t> by_bucket;
    for (const auto& e : events_) {
        if (query.slug && e.slug != *query.slug) continue;
        if (query.link_id && e.link_id != *query.link_id) continue;
        if (e.occurred_at < query.from || e.occurred_at >= query.to) continue;
        stats->total_attempts++;
        if (e.status_code >= 300 && e.status_code < 400) stats->successful_redirects++;
        by_status[e.status_code]++;
        by_domain[e.domain]++;
        if (query.bucket) {
            auto b = bucket_start(e.occurred_at, *query.bucket);
            by_bucket[std::chrono::duration_cast<std::chrono::seconds>(b.time_since_epoch()).count()]++;
        }
    }
    for (auto& [k,v] : by_status) stats->status_code_counts.push_back({static_cast<uint16_t>(k), v});
    for (auto& [k,v] : by_domain) stats->domain_counts.push_back({k, v});
    for (auto& [k,v] : by_bucket) stats->time_buckets.push_back({std::chrono::system_clock::time_point{std::chrono::seconds(k)}, v});
    std::sort(stats->time_buckets.begin(), stats->time_buckets.end(), [](auto& a, auto& b){ return a.bucket_start < b.bucket_start;});
    return true;
}

bool InMemoryClickEventRepository::DeleteOlderThan(analytics::Timestamp cutoff, analytics::AnalyticsError*) {
    std::scoped_lock lk(mu_);
    events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const auto& e){ return e.occurred_at < cutoff; }), events_.end());
    return true;
}

}

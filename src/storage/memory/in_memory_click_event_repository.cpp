#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

namespace url_shortener::storage::memory
{
namespace
{
using namespace analytics;
std::chrono::system_clock::time_point bucket_start(
    std::chrono::system_clock::time_point tp, AggregateBucket b)
{
    using namespace std::chrono;
    auto s = duration_cast<seconds>(tp.time_since_epoch()).count();
    long long width = b == AggregateBucket::hour
        ? 3600LL
        : (b == AggregateBucket::day ? 86400LL : 604800LL);
    return std::chrono::system_clock::time_point {seconds((s / width) * width)};
}
}  // namespace

bool InMemoryClickEventRepository::InsertBatch(
    const std::vector<analytics::ClickEvent>& events,
    analytics::AnalyticsError*)
{
    std::scoped_lock lk(mu_);
    events_.insert(events_.end(), events.begin(), events.end());
    return true;
}

bool InMemoryClickEventRepository::GetAggregateStats(
    const analytics::AggregateQuery& query,
    analytics::AggregateStats* stats,
    analytics::AnalyticsError*)
{
    if (!stats)
        return false;
    std::scoped_lock lk(mu_);
    *stats = {};
    std::unordered_map<int, std::uint64_t> by_status;
    std::unordered_map<std::string, std::uint64_t> by_domain;
    std::unordered_map<long long, std::uint64_t> by_bucket;
    for (const auto& e : events_) {
        if (query.slug && e.slug != *query.slug)
            continue;
        if (query.link_id && e.link_id != *query.link_id)
            continue;
        if (e.occurred_at < query.from || e.occurred_at >= query.to)
            continue;
        stats->total_attempts++;
        if (e.status_code >= 300 && e.status_code < 400)
            stats->successful_redirects++;
        by_status[e.status_code]++;
        by_domain[e.domain]++;
        if (query.bucket) {
            auto b = bucket_start(e.occurred_at, *query.bucket);
            by_bucket[std::chrono::duration_cast<std::chrono::seconds>(
                          b.time_since_epoch())
                          .count()]++;
        }
    }
    for (auto& [k, v] : by_status)
        stats->status_code_counts.push_back({static_cast<uint16_t>(k), v});
    for (auto& [k, v] : by_domain)
        stats->domain_counts.push_back({k, v});
    for (auto& [k, v] : by_bucket)
        stats->time_buckets.push_back(
            {std::chrono::system_clock::time_point {std::chrono::seconds(k)},
             v});
    std::sort(stats->time_buckets.begin(),
              stats->time_buckets.end(),
              [](auto& a, auto& b) { return a.bucket_start < b.bucket_start; });
    return true;
}

bool InMemoryClickEventRepository::DeleteOlderThan(analytics::Timestamp cutoff,
                                                   analytics::AnalyticsError*)
{
    std::scoped_lock lk(mu_);
    events_.erase(
        std::remove_if(events_.begin(),
                       events_.end(),
                       [&](const auto& e) { return e.occurred_at < cutoff; }),
        events_.end());
    return true;
}

bool InMemoryClickEventRepository::ListEvents(
    const analytics::ClickEventQuery& query,
    analytics::ClickEventPage* page,
    analytics::AnalyticsError* error)
{
    if (page == nullptr) {
        if (error != nullptr) {
            error->code = analytics::AnalyticsErrorCode::validation;
            error->message = "ListEvents requires a non-null page";
        }
        return false;
    }
    if (query.from && query.to && *query.from > *query.to) {
        if (error != nullptr) {
            error->code = analytics::AnalyticsErrorCode::validation;
            error->message = "ListEvents range is inverted (from > to)";
        }
        return false;
    }

    const auto to_lower = [](std::string s)
    {
        for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    };

    std::optional<std::string> text_lower;
    if (query.text) {
        text_lower = to_lower(*query.text);
    }

    std::scoped_lock lk(mu_);

    std::vector<const analytics::ClickEvent*> matches;
    for (const auto& e : events_) {
        if (query.slug && e.slug != *query.slug) {
            continue;
        }
        if (query.status_code && e.status_code != *query.status_code) {
            continue;
        }
        if (query.from && e.occurred_at < *query.from) {
            continue;
        }
        if (query.to && e.occurred_at >= *query.to) {
            continue;
        }
        if (text_lower) {
            const bool hit =
                to_lower(e.slug).find(*text_lower) != std::string::npos
                || to_lower(e.domain).find(*text_lower) != std::string::npos;
            if (!hit) {
                continue;
            }
        }
        matches.push_back(&e);
    }

    page->total = static_cast<std::uint64_t>(matches.size());

    const auto ordered_before =
        [&](const analytics::ClickEvent* a, const analytics::ClickEvent* b)
    {
        bool primary_less = false;
        bool primary_equal = false;
        switch (query.sort_by) {
            case analytics::ClickEventSort::slug:
                primary_less = a->slug < b->slug;
                primary_equal = a->slug == b->slug;
                break;
            case analytics::ClickEventSort::status_code:
                primary_less = a->status_code < b->status_code;
                primary_equal = a->status_code == b->status_code;
                break;
            case analytics::ClickEventSort::occurred_at:
                primary_less = a->occurred_at < b->occurred_at;
                primary_equal = a->occurred_at == b->occurred_at;
                break;
        }
        if (!primary_equal) {
            return query.descending ? !primary_less : primary_less;
        }
        // Stable final tie-break: event_id ascending, direction-independent.
        return a->event_id < b->event_id;
    };
    std::sort(matches.begin(), matches.end(), ordered_before);

    page->items.clear();
    const std::size_t clamped_limit =
        std::min(query.limit, analytics::kMaxClickEventPageSize);
    if (clamped_limit == 0 || query.offset >= matches.size()) {
        return true;
    }
    const std::size_t begin = query.offset;
    const std::size_t end = std::min(matches.size(), begin + clamped_limit);
    page->items.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        page->items.push_back(*matches[i]);
    }
    return true;
}

}  // namespace url_shortener::storage::memory

#include <chrono>

#include <url_shortener/analytics/aggregate_query.hpp>
#include <url_shortener/analytics/aggregate_stats.hpp>
#include <url_shortener/app/legacy_adapters.hpp>
#include <url_shortener/storage/link_repository.hpp>

#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

namespace url_shortener
{

namespace storage::memory
{
class InMemoryClickEventRepository;
}

storage::memory::InMemoryClickEventRepository& analyticsClickRepository();

namespace app
{
namespace
{

/// Lowest accepted Unix epoch for a stats window bound: 1970-01-01T00:00:00Z.
/// Rejecting negatives keeps the from/to span non-negative so the T-76 bucket
/// cap can be computed in whole seconds without a signed overflow (CWE-190).
constexpr long long k_min_epoch_seconds = 0;

/// Highest accepted Unix epoch for a stats window bound:
/// 9999-12-31T23:59:59Z. Bounding both ends to this range makes the maximum
/// possible `to - from` span ~2.5e11 seconds, which is far inside `long long`
/// (and inside `system_clock`'s nanosecond range) so neither the span subt
/// nor the time_point construction can overflow for absurd query values.
constexpr long long k_max_epoch_seconds = 253402300799;

/**
 * @brief Parses a decimal Unix-epoch-seconds string into a bounded seconds
 * count, rejecting anything a stats window must not accept.
 *
 * Validation happens on the integer BEFORE any `time_point` is built, so an
 * out-of-range value can never reach `system_clock`'s native-resolution
 * arithmetic (where an extreme epoch would overflow and silently skip the
 * T-76 cap — the bug this guard closes). Accepted input is a base-10 integer,
 * with no leading/trailing non-digit characters, whose value lies in
 * `[k_min_epoch_seconds, k_max_epoch_seconds]`
 * (1970-01-01T00:00:00Z .. 9999-12-31T23:59:59Z inclusive).
 *
 * @param value The raw `from`/`to` query string to parse.
 * @return The epoch in whole seconds when @p value is a fully-consumed decimal
 * integer within the accepted range; `std::nullopt` when it is non-numeric,
 * has trailing junk, over/underflows `long long`, or falls outside the range.
 * The caller maps a `std::nullopt` to the fixed `invalid_timestamp` detail and
 * never echoes @p value back.
 */
std::optional<long long> parseEpoch(const std::string& value)
{
    try {
        std::size_t pos = 0;
        const long long epoch = std::stoll(value, &pos);
        if (pos != value.size()) {
            return std::nullopt;
        }
        if (epoch < k_min_epoch_seconds || epoch > k_max_epoch_seconds) {
            return std::nullopt;
        }
        return epoch;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

AppError makeError(const AppErrorCode code, std::string detail)
{
    return AppError {code, std::move(detail)};
}

/// Maximum number of time buckets a single range-mode stats query may span
/// (threat model T-76, 2026-11-16 user decision). A wide span at `hour`
/// granularity would otherwise force ~10^5 bucket allocations per call
/// (CWE-770); this fixed bound caps that before any bucket is produced.
/// Exactly this many buckets is accepted; one more is rejected.
constexpr long long k_max_stat_buckets = 10000;

/// Bucket width in seconds for each accepted `bucket` granularity, used both to
/// select the aggregate bucket and to compute the span's bucket count for the
/// T-76 cap.
constexpr long long k_hour_bucket_seconds = 3600;
constexpr long long k_day_bucket_seconds = 86400;
constexpr long long k_week_bucket_seconds = 604800;

}  // namespace

bool LegacyLinkStore::create(const Link& link, AppError* error)
{
    RepoError repo_error = RepoError::permanent_failure;
    if (linkRepository().create(link, &repo_error)) {
        return true;
    }
    if (error != nullptr) {
        *error = repo_error == RepoError::already_exists
            ? makeError(AppErrorCode::slug_conflict, "slug already in use")
            : makeError(AppErrorCode::storage_failure, "repository failure");
    }
    return false;
}

std::optional<Link> LegacyLinkStore::findBySlug(const std::string& slug) const
{
    return getLinkForRead(slug);
}

std::optional<Link> LegacyLinkStore::findById(const std::string& id) const
{
    return linkRepository().getById(id);
}

bool LegacyLinkStore::slugExists(const std::string& slug) const
{
    return linkRepository().slugExists(slug);
}

void LegacyLinkStore::invalidateCache(const std::string& slug)
{
    linkCache().erase(slug);
}

bool LegacyLinkStore::update(const Link& link, AppError* error)
{
    if (updateLinkAndInvalidateCache(link)) {
        return true;
    }
    if (error != nullptr) {
        *error = makeError(AppErrorCode::storage_failure, "repository failure");
    }
    return false;
}

Result<LinkStatsView> LegacyLinkStatsReader::read(
    const GetLinkStatsQuery& query) const
{
    if (query.slug.empty()) {
        return {std::nullopt,
                makeError(AppErrorCode::not_found, "Link not found")};
    }

    const auto from_epoch = parseEpoch(query.from);
    const auto to_epoch = parseEpoch(query.to);
    if (!from_epoch.has_value() || !to_epoch.has_value()
        || *from_epoch >= *to_epoch)
    {
        return {std::nullopt,
                makeError(AppErrorCode::invalid_field, "invalid_timestamp")};
    }

    analytics::AggregateQuery aggregate_query;
    aggregate_query.slug = query.slug;
    aggregate_query.from =
        std::chrono::system_clock::time_point {std::chrono::seconds(*from_epoch)};
    aggregate_query.to =
        std::chrono::system_clock::time_point {std::chrono::seconds(*to_epoch)};
    long long bucket_width_seconds = 0;
    if (query.bucket == "hour") {
        aggregate_query.bucket = analytics::AggregateBucket::hour;
        bucket_width_seconds = k_hour_bucket_seconds;
    }
    else if (query.bucket == "day") {
        aggregate_query.bucket = analytics::AggregateBucket::day;
        bucket_width_seconds = k_day_bucket_seconds;
    }
    else if (query.bucket == "week") {
        aggregate_query.bucket = analytics::AggregateBucket::week;
        bucket_width_seconds = k_week_bucket_seconds;
    }
    else {
        return {std::nullopt,
                makeError(AppErrorCode::invalid_field, "invalid_bucket")};
    }

    // T-76 bucket-count cap: bound the span's bucket count BEFORE calling
    // GetAggregateStats or allocating any bucket. Ordered after the from<to and
    // invalid_bucket checks so a bad timestamp/bucket still reports its own
    // detail, never range_too_large. The span is computed in whole seconds
    // directly from the two validated int64 epochs — NOT by subtracting
    // system_clock time_points, whose nanosecond resolution would overflow for
    // extreme epochs and could wrap negative, skipping this cap. Both epochs are
    // bounded to [0, k_max_epoch_seconds] and `*from_epoch < *to_epoch` holds
    // here, so the subtraction is non-negative and far inside `long long`; the
    // count is (to - from) / width, exactly the number of whole buckets the span
    // covers. Over the cap -> a fixed, value-free detail that never echoes the
    // requested range (contract C6). Shared by REST GET .../stats and
    // link.stats, so the cap is identical for both (parity).
    const long long span_seconds = *to_epoch - *from_epoch;
    if (span_seconds / bucket_width_seconds > k_max_stat_buckets) {
        return {std::nullopt,
                makeError(AppErrorCode::invalid_field, "range_too_large")};
    }

    analytics::AggregateStats aggregate_stats;
    if (!analyticsClickRepository().GetAggregateStats(
            aggregate_query, &aggregate_stats, nullptr))
    {
        return {std::nullopt,
                makeError(AppErrorCode::storage_failure, "repository")};
    }

    LinkStatsView view;
    view.slug = query.slug;
    view.total_attempts = aggregate_stats.total_attempts;
    view.successful_redirects = aggregate_stats.successful_redirects;
    view.from = query.from;
    view.to = query.to;
    view.bucket = query.bucket;
    for (const auto& item : aggregate_stats.status_code_counts) {
        view.status_code_counts.push_back({item.status_code, item.count});
    }
    for (const auto& item : aggregate_stats.domain_counts) {
        view.domain_counts.push_back({item.domain, item.count});
    }
    for (const auto& item : aggregate_stats.time_buckets) {
        view.time_buckets.push_back(
            {std::chrono::duration_cast<std::chrono::seconds>(
                 item.bucket_start.time_since_epoch())
                 .count(),
             item.count});
    }
    return {view, {}};
}

}  // namespace app
}  // namespace url_shortener

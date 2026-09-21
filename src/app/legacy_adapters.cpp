#include <url_shortener/app/legacy_adapters.hpp>

#include <url_shortener/analytics/aggregate_query.hpp>
#include <url_shortener/analytics/aggregate_stats.hpp>
#include <url_shortener/storage/link_repository.h>
#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

#include <chrono>

namespace url_shortener {

namespace storage::memory {
class InMemoryClickEventRepository;
}

storage::memory::InMemoryClickEventRepository& analyticsClickRepository();

namespace app {
namespace {

std::optional<std::chrono::system_clock::time_point> parseEpoch(
    const std::string& value)
{
    try {
        std::size_t pos = 0;
        const long long epoch = std::stoll(value, &pos);
        if (pos != value.size()) {
            return std::nullopt;
        }
        return std::chrono::system_clock::time_point{std::chrono::seconds(epoch)};
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

AppError makeError(const AppErrorCode code, std::string detail)
{
    return AppError{code, std::move(detail)};
}

} // namespace

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

void LegacyLinkStore::update(const Link& link)
{
    updateLinkAndInvalidateCache(link);
}

Result<LinkStatsView> LegacyLinkStatsReader::read(
    const GetLinkStatsQuery& query) const
{
    if (query.slug.empty()) {
        return {std::nullopt, makeError(AppErrorCode::not_found, "Link not found")};
    }

    const auto from = parseEpoch(query.from);
    const auto to = parseEpoch(query.to);
    if (!from.has_value() || !to.has_value() || *from >= *to) {
        return {std::nullopt, makeError(AppErrorCode::invalid_field, "invalid_timestamp")};
    }

    analytics::AggregateQuery aggregate_query;
    aggregate_query.slug = query.slug;
    aggregate_query.from = *from;
    aggregate_query.to = *to;
    if (query.bucket == "hour") {
        aggregate_query.bucket = analytics::AggregateBucket::hour;
    }
    else if (query.bucket == "day") {
        aggregate_query.bucket = analytics::AggregateBucket::day;
    }
    else if (query.bucket == "week") {
        aggregate_query.bucket = analytics::AggregateBucket::week;
    }
    else {
        return {std::nullopt, makeError(AppErrorCode::invalid_field, "invalid_bucket")};
    }

    analytics::AggregateStats aggregate_stats;
    if (!analyticsClickRepository().GetAggregateStats(
            aggregate_query,
            &aggregate_stats,
            nullptr))
    {
        return {std::nullopt, makeError(AppErrorCode::storage_failure, "repository")};
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
        view.time_buckets.push_back({
            std::chrono::duration_cast<std::chrono::seconds>(
                item.bucket_start.time_since_epoch()).count(),
            item.count});
    }
    return {view, {}};
}

} // namespace app
} // namespace url_shortener

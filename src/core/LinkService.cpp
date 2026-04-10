#include "url_shortener/core/LinkService.hpp"

LinkService::LinkService(IMetadataRepository& repo, ICacheStore& cache, IAnalyticsSink& analytics, const IClock& clock)
    : repo_(repo)
    , cache_(cache)
    , analytics_(analytics)
    , clock_(clock)
{
}

ResolveResult LinkService::Resolve(const std::string& short_code)
{
    CacheError cache_error = CacheError::none;
    auto cached = cache_.Get(short_code, &cache_error);
    if (cache_error == CacheError::none && cached.has_value()) {
        const bool expired = cached->expires_at.has_value() && cached->expires_at.value() <= clock_.now();
        if (cached->is_active && !expired) {
            ResolveResult result{ResolveStatus::redirect, cached->target_url, true};
            EmitAnalytics(short_code, result);
            return result;
        }
    }

    auto record = repo_.GetByShortCode(short_code);
    if (!record.has_value()) {
        ResolveResult result{ResolveStatus::not_found, std::nullopt, false};
        EmitAnalytics(short_code, result);
        return result;
    }

    if (!record->is_active) {
        ResolveResult result{ResolveStatus::inactive, std::nullopt, false};
        EmitAnalytics(short_code, result);
        return result;
    }

    const bool expired = record->expires_at.has_value() && record->expires_at.value() <= clock_.now();
    if (expired) {
        ResolveResult result{ResolveStatus::expired, std::nullopt, false};
        EmitAnalytics(short_code, result);
        return result;
    }

    CacheValue value{record->target_url, record->expires_at, record->is_active, record->version};
    cache_.Set(short_code, value, std::nullopt);

    ResolveResult result{ResolveStatus::redirect, record->target_url, false};
    EmitAnalytics(short_code, result);
    return result;
}

bool LinkService::Update(const LinkRecord& record, RepoError* error)
{
    const bool updated = repo_.UpdateLink(record, error);
    cache_.Delete(record.short_code);
    return updated;
}

bool LinkService::Delete(const std::string& short_code, RepoError* error)
{
    const bool deleted = repo_.DeleteLink(short_code, error);
    cache_.Delete(short_code);
    return deleted;
}

void LinkService::EmitAnalytics(const std::string& short_code, const ResolveResult& result)
{
    uint16_t status_code = 500;
    switch (result.status) {
    case ResolveStatus::redirect:
        status_code = 302;
        break;
    case ResolveStatus::not_found:
        status_code = 404;
        break;
    case ResolveStatus::inactive:
    case ResolveStatus::expired:
        status_code = 410;
        break;
    }

    LinkAccessEvent event{short_code, clock_.now(), status_code, result.cache_hit, std::nullopt};
    analytics_.Emit(event);
}

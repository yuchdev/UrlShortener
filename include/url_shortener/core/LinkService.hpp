#pragma once

#include <optional>
#include <string>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/IAnalyticsSink.hpp"
#include "url_shortener/storage/ICacheStore.hpp"
#include "url_shortener/storage/IMetadataRepository.hpp"
#include "url_shortener/storage/models/RateLimitDecision.hpp"

/**
 * @brief Redirect-resolution outcome categories.
 */
enum class ResolveStatus {
    redirect,
    not_found,
    inactive,
    expired,
};

/**
 * @brief Redirect-resolution service output model.
 */
struct ResolveResult {
    ResolveStatus status = ResolveStatus::not_found;
    std::optional<std::string> target_url;
    bool cache_hit = false;
};

/**
 * @brief Core service that resolves links through repository/cache/analytics ports.
 */
class LinkService {
public:
    LinkService(IMetadataRepository& repo, ICacheStore& cache, IAnalyticsSink& analytics, const IClock& clock);

    ResolveResult Resolve(const std::string& short_code);
    bool Update(const LinkRecord& record, RepoError* error = nullptr);
    bool Delete(const std::string& short_code, RepoError* error = nullptr);

private:
    void EmitAnalytics(const std::string& short_code, const ResolveResult& result);

    IMetadataRepository& repo_;
    ICacheStore& cache_;
    IAnalyticsSink& analytics_;
    const IClock& clock_;
};

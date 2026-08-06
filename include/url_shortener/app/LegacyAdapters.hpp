#pragma once

#include <url_shortener/app/LinkCommandService.hpp>

namespace url_shortener {
namespace app {

class LegacyLinkStore final : public ILinkStore
{
public:
    bool create(const Link& link, AppError* error = nullptr) override;
    std::optional<Link> findBySlug(const std::string& slug) const override;
    std::optional<Link> findById(const std::string& id) const override;
    bool slugExists(const std::string& slug) const override;
    void invalidateCache(const std::string& slug) override;
};

class LegacyLinkStatsReader final : public ILinkStatsReader
{
public:
    Result<LinkStatsView> read(const GetLinkStatsQuery& query) const override;
};

} // namespace app
} // namespace url_shortener

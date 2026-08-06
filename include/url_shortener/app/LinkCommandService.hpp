#pragma once

#include <url_shortener/app/AppError.hpp>
#include <url_shortener/core/config.h>
#include <url_shortener/core/types.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace url_shortener {
namespace app {

struct CreateLinkCommand
{
    std::string target_url;
    std::optional<std::string> slug;
    std::optional<RedirectType> redirect_type;
    std::optional<std::string> expires_at;
    std::optional<bool> enabled;
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> metadata;
    std::optional<Link::Campaign> campaign;
};

enum class GetLinkBy
{
    slug,
    id
};

struct GetLinkQuery
{
    GetLinkBy by = GetLinkBy::slug;
    std::string value;
};

struct GetLinkStatsQuery
{
    std::string slug;
    std::string from;
    std::string to;
    std::string bucket;
};

struct StatusCodeCountView
{
    uint16_t status_code = 0;
    uint64_t count = 0;
};

struct DomainCountView
{
    std::string domain;
    uint64_t count = 0;
};

struct TimeBucketView
{
    int64_t bucket_start_epoch = 0;
    uint64_t count = 0;
};

struct LinkStatsView
{
    std::string slug;
    uint64_t total_attempts = 0;
    uint64_t successful_redirects = 0;
    std::vector<StatusCodeCountView> status_code_counts;
    std::vector<DomainCountView> domain_counts;
    std::vector<TimeBucketView> time_buckets;
    std::string from;
    std::string to;
    std::string bucket;
};

struct LinkView
{
    std::string id;
    std::string slug;
    std::string target_url;
    std::string short_url;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> expires_at;
    std::optional<std::string> deleted_at;
    bool enabled = true;
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> metadata;
    std::optional<Link::Campaign> campaign;
    Link::Stats stats;
    RedirectType redirect_type = RedirectType::temporary;
    LinkStatus status = LinkStatus::active;
};

class ILinkStore
{
public:
    virtual ~ILinkStore() = default;
    virtual bool create(const Link& link, AppError* error = nullptr) = 0;
    virtual std::optional<Link> findBySlug(const std::string& slug) const = 0;
    virtual std::optional<Link> findById(const std::string& id) const = 0;
    virtual bool slugExists(const std::string& slug) const = 0;
    virtual void invalidateCache(const std::string& slug) = 0;
};

class ILinkStatsReader
{
public:
    virtual ~ILinkStatsReader() = default;
    virtual Result<LinkStatsView> read(const GetLinkStatsQuery& query) const = 0;
};

class LinkCommandService
{
public:
    LinkCommandService(
        ILinkStore& store,
        ILinkStatsReader& stats_reader,
        const ServerConfig& config);

    Result<LinkView> CreateLink(const CreateLinkCommand& command) const;
    Result<LinkView> GetLink(const GetLinkQuery& query) const;
    Result<LinkStatsView> GetLinkStats(const GetLinkStatsQuery& query) const;

private:
    Result<std::string> chooseSlug(const CreateLinkCommand& command) const;
    LinkView toView(const Link& link) const;

    ILinkStore& store_;
    ILinkStatsReader& stats_reader_;
    const ServerConfig& config_;
};

std::string serializeLinkViewJson(const LinkView& link);
std::string serializeLinkStatsJson(const LinkStatsView& stats);

} // namespace app
} // namespace url_shortener

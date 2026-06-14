#pragma once

#include <url_shortener/core/types.h>
#include <optional>
#include <string>
#include <shared_mutex>
#include <unordered_map>

namespace url_shortener {

class IMetadataRepository
{
public:
    virtual ~IMetadataRepository() = default;
    virtual bool create(const Link& link, RepoError* error = nullptr) = 0;
    virtual std::optional<Link> getBySlug(const std::string& slug, RepoError* error = nullptr) const = 0;
    virtual std::optional<Link> getById(const std::string& id, RepoError* error = nullptr) const = 0;
    virtual bool slugExists(const std::string& slug, RepoError* error = nullptr) const = 0;
    virtual bool update(const Link& link, RepoError* error = nullptr) = 0;
};

class ILinkCacheStore
{
public:
    virtual ~ILinkCacheStore() = default;
    virtual std::optional<Link> get(const std::string& slug) = 0;
    virtual void set(const std::string& slug, const Link& link) = 0;
    virtual void erase(const std::string& slug) = 0;
};

class InMemoryMetadataRepository final : public IMetadataRepository
{
public:
    bool create(const Link& link, RepoError* error = nullptr) override;
    std::optional<Link> getBySlug(const std::string& slug, RepoError* error = nullptr) const override;
    std::optional<Link> getById(const std::string& id, RepoError* error = nullptr) const override;
    bool slugExists(const std::string& slug, RepoError* error = nullptr) const override;
    bool update(const Link& link, RepoError* error = nullptr) override;

private:
    mutable std::shared_mutex mutex_;  ///< Protects repository maps.
    std::unordered_map<std::string, Link> by_id_;  ///< Primary index: id -> Link.
    std::unordered_map<std::string, std::string> slug_to_id_;  ///< Secondary index: slug -> id.
};

class InMemoryLinkCacheStore final : public ILinkCacheStore
{
public:
    std::optional<Link> get(const std::string& slug) override;
    void set(const std::string& slug, const Link& link) override;
    void erase(const std::string& slug) override;

private:
    std::shared_mutex mutex_;  ///< Protects cache map.
    std::unordered_map<std::string, Link> by_slug_;  ///< Slug -> Link cache entries.
};

IMetadataRepository& linkRepository();
ILinkCacheStore& linkCache();

std::optional<Link> getLinkForRead(const std::string& slug);
bool updateLinkAndInvalidateCache(const Link& link);

} // namespace url_shortener

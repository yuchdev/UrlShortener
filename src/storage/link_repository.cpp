#include <url_shortener/storage/link_repository.h>
#include <mutex>

namespace url_shortener {

bool InMemoryMetadataRepository::create(const Link& link, RepoError* error)
{
    std::unique_lock lock(mutex_);
    if (slug_to_id_.find(link.slug) != slug_to_id_.end() || by_id_.find(link.id) != by_id_.end()) {
        if (error != nullptr) {
            *error = RepoError::already_exists;
        }
        return false;
    }
    slug_to_id_[link.slug] = link.id;
    by_id_[link.id] = link;
    return true;
}

std::optional<Link> InMemoryMetadataRepository::getBySlug(const std::string& slug, RepoError* error) const
{
    std::shared_lock lock(mutex_);
    if (const auto slug_it = slug_to_id_.find(slug); slug_it != slug_to_id_.end()) {
        if (const auto id_it = by_id_.find(slug_it->second); id_it != by_id_.end()) {
            return id_it->second;
        }
    }
    if (error != nullptr) {
        *error = RepoError::not_found;
    }
    return std::nullopt;
}

std::optional<Link> InMemoryMetadataRepository::getById(const std::string& id, RepoError* error) const
{
    std::shared_lock lock(mutex_);
    if (const auto it = by_id_.find(id); it != by_id_.end()) {
        return it->second;
    }
    if (error != nullptr) {
        *error = RepoError::not_found;
    }
    return std::nullopt;
}

bool InMemoryMetadataRepository::slugExists(const std::string& slug, RepoError*) const
{
    std::shared_lock lock(mutex_);
    return slug_to_id_.find(slug) != slug_to_id_.end();
}

bool InMemoryMetadataRepository::update(const Link& link, RepoError* error)
{
    std::unique_lock lock(mutex_);
    const auto it = by_id_.find(link.id);
    if (it == by_id_.end()) {
        if (error != nullptr) {
            *error = RepoError::not_found;
        }
        return false;
    }
    it->second = link;
    return true;
}

std::optional<Link> InMemoryLinkCacheStore::get(const std::string& slug)
{
    std::shared_lock lock(mutex_);
    if (const auto it = by_slug_.find(slug); it != by_slug_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void InMemoryLinkCacheStore::set(const std::string& slug, const Link& link)
{
    std::unique_lock lock(mutex_);
    by_slug_[slug] = link;
}

void InMemoryLinkCacheStore::erase(const std::string& slug)
{
    std::unique_lock lock(mutex_);
    by_slug_.erase(slug);
}

IMetadataRepository& linkRepository()
{
    static InMemoryMetadataRepository repo;
    return repo;
}

ILinkCacheStore& linkCache()
{
    static InMemoryLinkCacheStore cache;
    return cache;
}

std::optional<Link> getLinkForRead(const std::string& slug)
{
    if (const auto cached = linkCache().get(slug); cached.has_value()) {
        return cached;
    }

    const auto link = linkRepository().getBySlug(slug);
    if (link.has_value()) {
        linkCache().set(slug, *link);
    }
    return link;
}

bool updateLinkAndInvalidateCache(const Link& link)
{
    if (!linkRepository().update(link)) {
        return false;
    }
    linkCache().erase(link.slug);
    return true;
}

} // namespace url_shortener

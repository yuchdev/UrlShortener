#pragma once

#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <url_shortener/core/types.hpp>

namespace url_shortener
{

/**
 * @brief Legacy metadata repository port for the original two-index
 * in-memory storage API (`url_shortener` namespace). Superseded by
 * `IMetadataRepository` in `storage/i_metadata_repository.hpp` for
 * new code; retained for backward compatibility with handlers that
 * depend on the `Link` model from `core/types.hpp`.
 *
 * Thread-safety: implementations must be safe to call concurrently
 * from multiple request-handling threads.
 */
class IMetadataRepository
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed
     * through a base `IMetadataRepository*`.
     */
    virtual ~IMetadataRepository() = default;

    /**
     * @brief Persists a new link. Both `link.slug` and `link.id`
     * must be unique; either duplicate fails the call.
     * @param link Link to store; `link.id` and `link.slug` must
     * already be set by the caller.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::already_exists` when either the slug or the id is
     * already in use; left untouched on success.
     * @return `true` if the link was stored; `false` if the slug or
     * id is already in use.
     */
    virtual bool create(const Link& link, RepoError* error = nullptr) = 0;

    /**
     * @brief Looks up a link by its short-code slug.
     * @param slug Slug to look up.
     * @param[out] error If non-null and the lookup fails (not found
     * or backend error), receives `RepoError::not_found`; left
     * untouched on success.
     * @return The matching link; `std::nullopt` if not found.
     */
    virtual std::optional<Link> getBySlug(const std::string& slug,
                                          RepoError* error = nullptr) const = 0;

    /**
     * @brief Looks up a link by its stable identifier.
     * @param id Stable link identifier to look up.
     * @param[out] error If non-null and the lookup fails (not found
     * or backend error), receives `RepoError::not_found`; left
     * untouched on success.
     * @return The matching link; `std::nullopt` if not found.
     */
    virtual std::optional<Link> getById(const std::string& id,
                                        RepoError* error = nullptr) const = 0;

    /**
     * @brief Tests whether a slug is registered.
     * @param slug Slug to check.
     * @param[out] error Unused by the in-memory implementation;
     * backends that can fail may write a failure reason here.
     * @return `true` if a link with this slug exists; `false`
     * otherwise.
     */
    virtual bool slugExists(const std::string& slug,
                            RepoError* error = nullptr) const = 0;

    /**
     * @brief Replaces an existing link record (looked up by
     * `link.id`).
     * @param link Updated link record; the backend finds the
     * existing record by `link.id`.
     * @param[out] error If non-null and the call fails, receives
     * `RepoError::not_found` when no record exists for `link.id`;
     * left untouched on success.
     * @return `true` on success; `false` if no record exists for
     * `link.id`.
     */
    virtual bool update(const Link& link, RepoError* error = nullptr) = 0;
};

/**
 * @brief Legacy cache-aside store port for the original in-memory
 * slug-to-Link cache. Superseded by `ICacheStore` in
 * `storage/i_cache_store.hpp` for new code.
 *
 * Thread-safety: implementations must be safe to call concurrently
 * from multiple request-handling threads.
 */
class ILinkCacheStore
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed
     * through a base `ILinkCacheStore*`.
     */
    virtual ~ILinkCacheStore() = default;

    /**
     * @brief Looks up a cached link by slug.
     * @param slug Slug to look up in the cache.
     * @return The cached `Link` if the slug is present; `std::nullopt`
     * on a cache miss.
     */
    virtual std::optional<Link> get(const std::string& slug) = 0;

    /**
     * @brief Stores or overwrites a cached link.
     * @param slug Cache key.
     * @param link Link value to cache.
     */
    virtual void set(const std::string& slug, const Link& link) = 0;

    /**
     * @brief Evicts a slug from the cache. Idempotent: a no-op when
     * the slug is not cached.
     * @param slug Cache key to evict.
     */
    virtual void erase(const std::string& slug) = 0;
};

/**
 * @brief Thread-safe in-memory `IMetadataRepository` implementation
 * backed by two hash maps: a primary `id → Link` index and a
 * secondary `slug → id` index. Used as the default singleton
 * repository returned by `linkRepository()`.
 *
 * Thread-safety: all public methods acquire `mutex_` appropriately
 * (`shared_lock` for reads, `unique_lock` for writes).
 */
class InMemoryMetadataRepository final : public IMetadataRepository
{
public:
    /**
     * @brief Creates a new entry in both `by_id_` and `slug_to_id_`.
     * @see IMetadataRepository::create
     */
    bool create(const Link& link, RepoError* error = nullptr) override;

    /**
     * @brief Looks up a link via the `slug_to_id_` secondary index.
     * @see IMetadataRepository::getBySlug
     */
    std::optional<Link> getBySlug(const std::string& slug,
                                  RepoError* error = nullptr) const override;

    /**
     * @brief Looks up a link directly in the `by_id_` primary index.
     * @see IMetadataRepository::getById
     */
    std::optional<Link> getById(const std::string& id,
                                RepoError* error = nullptr) const override;

    /**
     * @brief Tests the `slug_to_id_` secondary index for key
     * presence; always returns `false` for an unknown slug without
     * writing to `error`.
     * @see IMetadataRepository::slugExists
     */
    bool slugExists(const std::string& slug,
                    RepoError* error = nullptr) const override;

    /**
     * @brief Replaces the entry in `by_id_`; does not update
     * `slug_to_id_`.
     * @see IMetadataRepository::update
     */
    bool update(const Link& link, RepoError* error = nullptr) override;

private:
    ///< Protects repository maps.
    mutable std::shared_mutex mutex_;

    ///< Primary index: id -> Link.
    std::unordered_map<std::string, Link> by_id_;

    ///< Secondary index: slug -> id.
    std::unordered_map<std::string, std::string> slug_to_id_;
};

/**
 * @brief Thread-safe in-memory `ILinkCacheStore` implementation
 * backed by a single `slug → Link` hash map. Used as the default
 * singleton cache returned by `linkCache()`.
 *
 * Thread-safety: all public methods acquire `mutex_` appropriately.
 */
class InMemoryLinkCacheStore final : public ILinkCacheStore
{
public:
    /**
     * @brief Returns the cached link for `slug`, or `std::nullopt`
     * on a miss.
     * @see ILinkCacheStore::get
     */
    std::optional<Link> get(const std::string& slug) override;

    /**
     * @brief Stores or overwrites the entry for `slug`.
     * @see ILinkCacheStore::set
     */
    void set(const std::string& slug, const Link& link) override;

    /**
     * @brief Erases the entry for `slug`; a no-op when absent.
     * @see ILinkCacheStore::erase
     */
    void erase(const std::string& slug) override;

private:
    ///< Protects cache map.
    std::shared_mutex mutex_;

    ///< Slug -> Link cache entries.
    std::unordered_map<std::string, Link> by_slug_;
};

/**
 * @brief Returns the process-wide singleton `IMetadataRepository`.
 *
 * The singleton is an `InMemoryMetadataRepository` initialized on
 * first call (Meyers singleton, thread-safe per C++11). Its lifetime
 * spans the entire process; callers must not destroy it.
 * @return Reference to the shared `InMemoryMetadataRepository`.
 */
IMetadataRepository& linkRepository();

/**
 * @brief Returns the process-wide singleton `ILinkCacheStore`.
 *
 * Initialized on first call (Meyers singleton, thread-safe per
 * C++11). Its lifetime spans the entire process; callers must not
 * destroy it.
 * @return Reference to the shared `InMemoryLinkCacheStore`.
 */
ILinkCacheStore& linkCache();

/**
 * @brief Cache-aside read for a link by slug.
 *
 * Checks `linkCache()` first. On a cache miss, reads from
 * `linkRepository()` and, if found, populates the cache best-effort.
 * @param slug Slug to look up.
 * @return The matching link; `std::nullopt` if not found in either
 * the cache or the repository.
 */
std::optional<Link> getLinkForRead(const std::string& slug);

/**
 * @brief Persists an updated link and invalidates its cache entry.
 *
 * Calls `linkRepository().update(link)`, then — only on success —
 * calls `linkCache().erase(link.slug)` to remove any stale cached
 * copy.
 * @param link Updated link record to persist; `link.id` must already
 * exist in the repository.
 * @return `true` if the repository update succeeded (and the cache
 * entry was evicted); `false` if `update` returned `false`.
 */
bool updateLinkAndInvalidateCache(const Link& link);

}  // namespace url_shortener

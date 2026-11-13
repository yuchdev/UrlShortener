#pragma once

#include <shared_mutex>
#include <unordered_map>

#include "url_shortener/storage/i_metadata_repository.hpp"

/**
 * @brief Thread-safe in-memory `IMetadataRepository` backed by a
 * single `short_code → LinkRecord` hash map. Used as the default
 * backend by `StorageFactory` when no external store is configured.
 *
 * All public methods acquire `mutex_` appropriately (`shared_lock`
 * for read-only lookups, `unique_lock` for mutations). The
 * implementation never fails due to backend errors; every method
 * either succeeds or returns a logic error (`not_found`,
 * `already_exists`, `invalid_argument`). `DeleteLink` is idempotent
 * and always returns `true`.
 *
 * @see IMetadataRepository
 */
class InMemoryMetadataRepository final : public IMetadataRepository
{
public:
    /**
     * @brief Validates `request` via `isValid()`, then inserts a new
     * `LinkRecord` into `by_code_` keyed on `request.short_code`.
     * @see IMetadataRepository::CreateLink
     * @note Sets `*error = RepoError::none` on success (note:
     * the interface contract says error is left untouched on success;
     * this implementation writes `none` as a convenience).
     */
    bool CreateLink(const CreateLinkRequest& request,
                    const std::string& id,
                    const std::chrono::system_clock::time_point& now,
                    RepoError* error = nullptr) override;

    /**
     * @brief Looks up a `LinkRecord` in `by_code_` by `short_code`.
     * @see IMetadataRepository::GetByShortCode
     */
    std::optional<LinkRecord> GetByShortCode(
        const std::string& short_code,
        RepoError* error = nullptr) const override;

    /**
     * @brief Replaces the entry in `by_code_` keyed on
     * `record.short_code`; returns `false` with `not_found` if the
     * key is absent.
     * @see IMetadataRepository::UpdateLink
     */
    bool UpdateLink(const LinkRecord& record,
                    RepoError* error = nullptr) override;

    /**
     * @brief Erases the entry for `short_code` from `by_code_`;
     * always returns `true` (idempotent, no error on missing key).
     * @see IMetadataRepository::DeleteLink
     */
    bool DeleteLink(const std::string& short_code,
                    RepoError* error = nullptr) override;

    /**
     * @brief Filters `by_code_` by the query predicates, sorts by
     * `created_at` ascending, then returns the page described by
     * `query.offset`/`query.limit`.
     * @see IMetadataRepository::ListLinks
     */
    std::vector<LinkRecord> ListLinks(
        const ListLinksQuery& query, RepoError* error = nullptr) const override;

    /**
     * @brief Delegates to `GetByShortCode` and returns whether the
     * result is non-empty; always writes `RepoError::none` to
     * `*error` before returning.
     * @see IMetadataRepository::Exists
     */
    bool Exists(const std::string& short_code,
                RepoError* error = nullptr) const override;

private:
    ///< Guards `by_code_`; `mutable` so that const read methods can
    ///< acquire a shared lock.
    mutable std::shared_mutex mutex_;

    ///< Primary index: `short_code → LinkRecord`. Guarded by
    ///< `mutex_`.
    std::unordered_map<std::string, LinkRecord> by_code_;
};

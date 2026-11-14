#pragma once

/**
 * @file repository_link_store_fakes.hpp
 * @brief Shared test doubles and builders for the
 * `app::RepositoryLinkStore` unit tests (subtasks 03 reads and 04
 * writes).
 *
 * Provides a map-backed `::IMetadataRepository` fake with per-method
 * call counters and injectable `::RepoError`s (covering both the read
 * and the write paths), a recording `::ICacheStore` fake that proves
 * link reads never consult it (threat model T-38) and that its `Delete`
 * failures are swallowed, and the `makeLink`/`makeRecord` builders that
 * round-trip through the production codec. The including translation
 * unit must define `BOOST_TEST_MODULE` and include
 * `<boost/test/unit_test.hpp>` before this header (for
 * `BOOST_REQUIRE_MESSAGE` in `makeRecord`).
 */

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/errors/cache_error.hpp>
#include <url_shortener/storage/errors/repo_error.hpp>
#include <url_shortener/storage/i_cache_store.hpp>
#include <url_shortener/storage/i_metadata_repository.hpp>
#include <url_shortener/storage/models/create_link_request.hpp>
#include <url_shortener/storage/models/link_record.hpp>
#include <url_shortener/storage/models/list_links_query.hpp>

/// Fixed clock origin for deterministic TTL arithmetic.
inline const std::chrono::system_clock::time_point kT0 =
    std::chrono::system_clock::time_point(std::chrono::seconds(1'000'000));

/**
 * @brief Builds a minimal valid link with the given slug/id.
 * @param slug Slug for the link; id is derived as `"id-" + slug`.
 * @return A populated link suitable for codec round-tripping.
 */
inline url_shortener::Link makeLink(const std::string& slug)
{
    url_shortener::Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = "2026-01-01T00:00:00Z";
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = url_shortener::RedirectType::temporary;
    return link;
}

/**
 * @brief Encodes a link to a `::LinkRecord` via the production codec.
 * @param link Link to encode.
 * @return The decodable record (the test aborts if encoding fails).
 */
inline ::LinkRecord makeRecord(const url_shortener::Link& link)
{
    ::LinkRecord record;
    std::string error;
    BOOST_REQUIRE_MESSAGE(
        url_shortener::app::link_to_record(link, &record, &error),
        "link_to_record failed: " << error);
    return record;
}

/**
 * @brief Map-backed `::IMetadataRepository` test double that counts
 * calls, can inject repository errors, and persists writes.
 *
 * Records are stored in insertion order (so `ListLinks` paginates
 * deterministically) and keyed by `short_code`. Every method increments
 * a per-method counter; each read/write method has an injectable
 * `::RepoError` that, when non-`none`, is returned instead of running.
 *
 * Write behavior mirrors a real backend closely enough for the write
 * tests: `CreateLink` rejects an invalid request (`invalid_argument`)
 * and a duplicate `short_code` (`already_exists`) before storing a new
 * record built from the request + id + `now`; `UpdateLink` replaces the
 * record matched by `short_code`, or reports `not_found` when none
 * matches. `GetById` searches by `LinkRecord::id` and returns
 * inactive/soft-deleted records too (no status filter), mirroring the
 * port contract.
 */
class FakeMetadataRepository final : public ::IMetadataRepository
{
public:
    ///< Records in insertion order; also the pagination order.
    std::vector<::LinkRecord> records;

    ///< Number of `CreateLink` calls observed.
    mutable int create_calls = 0;

    ///< Number of `GetByShortCode` calls observed.
    mutable int get_calls = 0;

    ///< Number of `GetById` calls observed.
    mutable int get_by_id_calls = 0;

    ///< Number of `UpdateLink` calls observed.
    mutable int update_calls = 0;

    ///< Number of `ListLinks` (page) calls observed.
    mutable int list_calls = 0;

    ///< Number of `Exists` calls observed.
    mutable int exists_calls = 0;

    ///< Error returned by `CreateLink` when not `none`.
    ::RepoError create_error = ::RepoError::none;

    ///< Error returned by `GetByShortCode` when not `none`.
    ::RepoError get_error = ::RepoError::none;

    ///< Error returned by `GetById` when not `none`.
    ::RepoError get_by_id_error = ::RepoError::none;

    ///< Error returned by `UpdateLink` when not `none`.
    ::RepoError update_error = ::RepoError::none;

    ///< Error returned by `ListLinks` when not `none`.
    ::RepoError list_error = ::RepoError::none;

    ///< Error returned by `Exists` when not `none`.
    ::RepoError exists_error = ::RepoError::none;

    ///< Result returned by `Exists` when no error is injected.
    bool exists_result = true;

    /**
     * @brief Inserts or overwrites a record by short code.
     * @param record Record to store.
     */
    void put(const ::LinkRecord& record)
    {
        for (auto& existing : records) {
            if (existing.short_code == record.short_code) {
                existing = record;
                return;
            }
        }
        records.push_back(record);
    }

    /**
     * @brief Returns the stored record for `short_code`, if present.
     * @param short_code Short code to look up.
     * @return Pointer to the stored record, or `nullptr` if absent. The
     * pointer is invalidated by any later `put`/`CreateLink`.
     */
    const ::LinkRecord* find(const std::string& short_code) const
    {
        for (const auto& record : records) {
            if (record.short_code == short_code) {
                return &record;
            }
        }
        return nullptr;
    }

    bool CreateLink(const ::CreateLinkRequest& request,
                    const std::string& id,
                    const std::chrono::system_clock::time_point& now,
                    ::RepoError* error = nullptr) override
    {
        ++create_calls;
        if (create_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = create_error;
            }
            return false;
        }
        if (!request.isValid()) {
            if (error != nullptr) {
                *error = ::RepoError::invalid_argument;
            }
            return false;
        }
        if (find(request.short_code) != nullptr) {
            if (error != nullptr) {
                *error = ::RepoError::already_exists;
            }
            return false;
        }
        ::LinkRecord record;
        record.id = id;
        record.short_code = request.short_code;
        record.target_url = request.target_url;
        record.created_at = now;
        record.updated_at = now;
        record.expires_at = request.expires_at;
        record.owner = request.owner;
        record.attributes = request.attributes;
        record.is_active = request.is_active;
        records.push_back(record);
        return true;
    }

    std::optional<::LinkRecord> GetByShortCode(
        const std::string& short_code,
        ::RepoError* error = nullptr) const override
    {
        ++get_calls;
        if (get_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = get_error;
            }
            return std::nullopt;
        }
        for (const auto& record : records) {
            if (record.short_code == short_code) {
                return record;
            }
        }
        return std::nullopt;
    }

    std::optional<::LinkRecord> GetById(
        const std::string& id, ::RepoError* error = nullptr) const override
    {
        ++get_by_id_calls;
        if (get_by_id_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = get_by_id_error;
            }
            return std::nullopt;
        }
        for (const auto& record : records) {
            if (record.id == id) {
                return record;  // inactive/soft-deleted returned too
            }
        }
        return std::nullopt;
    }

    bool UpdateLink(const ::LinkRecord& record,
                    ::RepoError* error = nullptr) override
    {
        ++update_calls;
        if (update_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = update_error;
            }
            return false;
        }
        for (auto& existing : records) {
            if (existing.short_code == record.short_code) {
                existing = record;
                return true;
            }
        }
        if (error != nullptr) {
            *error = ::RepoError::not_found;
        }
        return false;
    }

    bool DeleteLink(const std::string&, ::RepoError* = nullptr) override
    {
        return true;
    }

    std::vector<::LinkRecord> ListLinks(
        const ::ListLinksQuery& query,
        ::RepoError* error = nullptr) const override
    {
        ++list_calls;
        if (list_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = list_error;
            }
            return {};
        }
        std::vector<::LinkRecord> filtered;
        for (const auto& record : records) {
            if (!query.include_inactive && !record.is_active) {
                continue;
            }
            if (!query.include_deleted && record.deleted_at.has_value()) {
                continue;
            }
            filtered.push_back(record);
        }
        std::vector<::LinkRecord> page;
        for (std::size_t i = query.offset;
             i < filtered.size() && page.size() < query.limit;
             ++i)
        {
            page.push_back(filtered[i]);
        }
        return page;
    }

    bool Exists(const std::string&, ::RepoError* error = nullptr) const override
    {
        ++exists_calls;
        if (exists_error != ::RepoError::none) {
            if (error != nullptr) {
                *error = exists_error;
            }
            return false;
        }
        return exists_result;
    }
};

/**
 * @brief `::ICacheStore` test double that records `Delete` calls and
 * proves `Get` is never invoked for link data (threat model T-38).
 *
 * A preset miss is returned from `Get` so a test can assert the store's
 * reads never consult it; `get_calls` must stay zero. `Delete` can be
 * made to report a backend failure (`delete_fails`) or to throw
 * (`delete_throws`) to exercise the "cache failures never fail a
 * request" contract on the write paths.
 */
class RecordingCacheStore final : public ::ICacheStore
{
public:
    ///< Number of `Get` calls observed; must remain zero.
    mutable int get_calls = 0;

    ///< Number of `Delete` calls observed.
    int delete_calls = 0;

    ///< Last key passed to `Delete`.
    std::string last_deleted_key;

    ///< When true, `Delete` reports a backend failure (`false`).
    bool delete_fails = false;

    ///< When true, `Delete` throws instead of returning.
    bool delete_throws = false;

    std::optional<CacheValue> Get(const std::string&,
                                  ::CacheError* = nullptr) override
    {
        ++get_calls;
        return std::nullopt;
    }

    bool Set(const std::string&,
             const CacheValue&,
             std::optional<std::chrono::seconds>,
             ::CacheError* = nullptr) override
    {
        return true;
    }

    bool Delete(const std::string& key, ::CacheError* error = nullptr) override
    {
        ++delete_calls;
        last_deleted_key = key;
        if (delete_throws) {
            throw std::runtime_error("cache delete failure");
        }
        if (delete_fails) {
            if (error != nullptr) {
                *error = ::CacheError::unavailable;
            }
            return false;
        }
        return true;
    }

    bool ClearByPrefix(const std::string&, ::CacheError* = nullptr) override
    {
        return true;
    }
};

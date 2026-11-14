#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "url_shortener/storage/errors/repo_error.hpp"
#include "url_shortener/storage/models/create_link_request.hpp"
#include "url_shortener/storage/models/link_record.hpp"
#include "url_shortener/storage/models/list_links_query.hpp"

/**
 * @brief Storage port for short-link CRUD and lifecycle operations
 * (see `CLAUDE.md` "Storage: ports and adapters"). Implemented per
 * backend under `src/storage/`. Expected failures are reported via
 * the `bool` + `*error` out-param convention, never by throwing.
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
     * @brief Persists a new short-link record.
     * @param request Link creation parameters; must pass
     * `request.isValid()` or the call returns `false` with
     * `RepoError::invalid_argument`.
     * @param id Caller-assigned stable identifier for the new record;
     * must be unique across all existing records.
     * @param now Wall-clock creation timestamp; stored as
     * `created_at` and `updated_at` on the resulting `LinkRecord`.
     * @param[out] error If non-null and the call fails, receives the
     * failure reason; left untouched on success.
     * @return `true` if the record was created; `false` if
     * `request.isValid()` returns `false` (`invalid_argument`), if
     * a record with the same `short_code` already exists
     * (`already_exists`), or if the backend failed.
     */
    virtual bool CreateLink(const CreateLinkRequest& request,
                            const std::string& id,
                            const std::chrono::system_clock::time_point& now,
                            RepoError* error = nullptr) = 0;

    /**
     * @brief Looks up a record by its public short-code slug.
     * @param short_code Short code to look up.
     * @param[out] error If non-null and a backend error occurs (not
     * a plain not-found), receives the failure reason; left untouched
     * on success and when the code simply does not exist.
     * @return The matching `LinkRecord` if found; `std::nullopt` if
     * the short code does not exist or a backend error occurred.
     */
    virtual std::optional<LinkRecord> GetByShortCode(
        const std::string& short_code, RepoError* error = nullptr) const = 0;

    /**
     * @brief Looks up a record by its stable caller-assigned id.
     *
     * Returns the record regardless of lifecycle status: an inactive
     * (`is_active == false`) or soft-deleted record is still returned, so
     * admin paths (`link get --id`) can inspect disabled/deleted links. This
     * mirrors GetByShortCode, which also does not filter by status.
     *
     * @param id Stable identifier to look up (the `LinkRecord::id` assigned
     * at CreateLink). An empty id never matches and returns `std::nullopt`
     * with `error` untouched.
     * @param[out] error If non-null and a backend error occurs (not a plain
     * not-found), receives the failure reason; left untouched on success and
     * when the id simply does not exist.
     * @return The matching `LinkRecord` if found; `std::nullopt` if the id
     * does not exist or a backend error occurred (callers distinguish the two
     * by inspecting `error`).
     */
    virtual std::optional<LinkRecord> GetById(
        const std::string& id, RepoError* error = nullptr) const = 0;

    /**
     * @brief Replaces the mutable fields of an existing record.
     * @param record Updated record; the backend looks it up by
     * `record.short_code`.
     * @param[out] error If non-null and the call fails, receives the
     * failure reason; left untouched on success.
     * @return `true` on success; `false` if no record exists with
     * `record.short_code` (`not_found`) or if the backend failed.
     */
    virtual bool UpdateLink(const LinkRecord& record,
                            RepoError* error = nullptr) = 0;

    /**
     * @brief Removes a record by short code. Idempotent: returns
     * `true` even when the short code does not exist.
     * @param short_code Short code of the record to delete.
     * @param[out] error If non-null and a backend error occurs,
     * receives the failure reason; left untouched on success
     * (including when the code was absent).
     * @return `true` on success (including when the code did not
     * exist); `false` if the backend failed.
     */
    virtual bool DeleteLink(const std::string& short_code,
                            RepoError* error = nullptr) = 0;

    /**
     * @brief Returns a page of records matching the query.
     *
     * Records are sorted by `created_at` ascending before the
     * `offset`/`limit` pagination window is applied.
     * @param query Filter, sort, and pagination options; see
     * `ListLinksQuery` for field semantics.
     * @param[out] error If non-null and a backend error occurs,
     * receives the failure reason; left untouched on success
     * (including when the query matches no records).
     * @return The matching page of records; an empty vector when
     * no records match the filters, `query.offset` is beyond the
     * result set, or a backend error occurred.
     */
    virtual std::vector<LinkRecord> ListLinks(
        const ListLinksQuery& query, RepoError* error = nullptr) const = 0;

    /**
     * @brief Tests whether a short code is registered.
     *
     * A `false` return means the code does not exist — that is not
     * an error from this method's perspective. `error` is only set
     * when the backend itself fails.
     * @param short_code Short code to check.
     * @param[out] error If non-null and a backend error occurs,
     * receives the failure reason; left untouched on success (even
     * when returning `false`).
     * @return `true` if a record with `short_code` exists; `false`
     * if it does not or if the backend failed.
     */
    virtual bool Exists(const std::string& short_code,
                        RepoError* error = nullptr) const = 0;
};

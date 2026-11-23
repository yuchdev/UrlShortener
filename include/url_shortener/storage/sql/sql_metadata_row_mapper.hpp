#pragma once

#include "url_shortener/storage/models/create_link_request.hpp"
#include "url_shortener/storage/models/link_record.hpp"
#include "url_shortener/storage/sql/i_sql_session_factory.hpp"

/**
 * @brief Converts between `LinkRecord`/`CreateLinkRequest` domain models and
 * the `SqlRow` wire model consumed by `ISqlSession`. Timestamps are expressed
 * as Unix epoch seconds (`long long`) in `SqlRow` and as
 * `std::chrono::system_clock::time_point` in domain models. The `id` and
 * `attributes` map are serialized to/from a JSON blob stored in
 * `SqlRow::attributes_json`.
 */
class SqlMetadataRowMapper
{
public:
    /**
     * @brief Builds a `SqlRow` for an INSERT from a create request.
     * @param request Link creation parameters; `short_code`, `target_url`,
     * and `is_active` must be set before calling.
     * @param id Caller-assigned stable link identifier stored in
     * `attributes_json`.
     * @param now Wall-clock time used for `created_at` and `updated_at`.
     * @return A fully-populated `SqlRow` ready to pass to
     * `ISqlSession::InsertLink`.
     */
    SqlRow ToCreateRow(const CreateLinkRequest& request,
                       const std::string& id,
                       const std::chrono::system_clock::time_point& now) const;

    /**
     * @brief Builds a `SqlRow` for an UPDATE from an existing link record.
     * @param record The updated domain object; `short_code` is used as the
     * UPDATE key.
     * @return A fully-populated `SqlRow` ready to pass to
     * `ISqlSession::UpdateLink`.
     */
    SqlRow ToUpdateRow(const LinkRecord& record) const;

    /**
     * @brief Populates a `LinkRecord` from a raw SQL row.
     * @param row Row returned by `ISqlSession::SelectByShortCode` or
     * `ISqlSession::List`; `short_code`, `target_url`, `created_at`,
     * `updated_at`, and `is_active` must be present.
     * @param[out] out Receives the mapped `LinkRecord` on success; left in
     * an indeterminate state on failure.
     * @param[out] error If non-null and mapping fails, receives
     * `RepoError::permanent_failure` (missing required fields or
     * `attributes_json` parse error); set to `RepoError::none` on success.
     * @return `true` if all required fields were present and
     * `attributes_json` (if present) parsed successfully; `false` otherwise.
     */
    bool FromRow(const SqlRow& row, LinkRecord* out, RepoError* error) const;
};

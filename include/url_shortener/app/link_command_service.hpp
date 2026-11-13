#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief Command to create a new short link.
 *
 * All optional fields fall back to `ServerConfig` defaults or are
 * omitted from the created record when absent.
 */
struct CreateLinkCommand
{
    ///< Destination URL; must be an absolute HTTP/HTTPS URL.
    ///< Validated and normalized by `LinkCommandService::CreateLink`.
    std::string target_url;

    ///< Custom slug; if absent, one is generated randomly using
    ///< `ServerConfig::shortener_generated_slug_length`.
    std::optional<std::string> slug;

    ///< Redirect type (temporary/permanent); if absent, falls back
    ///< to `ServerConfig::shortener_default_redirect_type`.
    std::optional<RedirectType> redirect_type;

    ///< Expiry timestamp in RFC3339 UTC (e.g.
    ///< `"2025-12-31T23:59:59Z"`); if absent and
    ///< `ServerConfig::shortener_default_expiry_seconds` is set, a
    ///< default expiry is computed at creation time.
    std::optional<std::string> expires_at;

    ///< Initial enabled state; defaults to `true` when absent.
    std::optional<bool> enabled;

    ///< User-supplied tag list; subject to `validateTags`
    ///< constraints (count and per-tag length limits).
    std::vector<std::string> tags;

    ///< User-supplied flat string-to-string metadata map; subject
    ///< to `validateMetadata` key-count and key/value-length limits.
    std::unordered_map<std::string, std::string> metadata;

    ///< Optional UTM-style campaign attribution; validated by
    ///< `validateCampaign` for per-field length limits.
    std::optional<Link::Campaign> campaign;
};

/**
 * @brief Discriminator for `GetLinkQuery`: selects which identifier
 * to look up a link by.
 */
enum class GetLinkBy
{
    ///< Look up by the public short-code slug.
    slug,

    ///< Look up by the stable internal link identifier.
    id
};

/**
 * @brief Query to retrieve a single link by slug or stable ID.
 */
struct GetLinkQuery
{
    ///< Selects whether `value` is a slug or a stable ID.
    GetLinkBy by = GetLinkBy::slug;

    ///< The slug or stable ID to look up, depending on `by`.
    std::string value;
};

/**
 * @brief Partial-update command for an existing link (PATCH semantics).
 *
 * Every mutable field is wrapped so that "field absent" (leave untouched) is
 * distinct from "field present". For @ref expires_at and @ref campaign a
 * second, inner optional additionally distinguishes an explicit `null`
 * (clear the value) from a concrete new value. Collapsing either optional
 * layer silently changes PATCH semantics, so both are preserved verbatim from
 * the JSON `field present vs. absent vs. explicit null` distinction the REST
 * handler implements today.
 */
struct UpdateLinkCommand
{
    ///< Slug of the link to update (required).
    std::string slug;

    /// Present => set enabled flag; absent => leave untouched.
    std::optional<bool> enabled;

    /// Outer optional = field present; inner optional = value (nullopt =>
    /// explicit null, i.e. clear the expiry).
    std::optional<std::optional<std::string>> expires_at;

    /// Present => replace the full tag list; absent => leave untouched.
    std::optional<std::vector<std::string>> tags;

    /// Present => replace the full metadata map; absent => leave untouched.
    std::optional<std::unordered_map<std::string, std::string>> metadata;

    /// Outer optional = field present; inner optional = value (nullopt =>
    /// explicit null, i.e. clear the campaign).
    std::optional<std::optional<Link::Campaign>> campaign;
};

/**
 * @brief Soft-delete command for an existing link.
 *
 * Sets @c deleted_at without removing the record, mirroring the REST
 * `DELETE /api/v1/links/{slug}` behavior.
 */
struct DeleteLinkCommand
{
    ///< Slug of the link to soft-delete (required).
    std::string slug;
};

/**
 * @brief Enable/disable command for an existing link.
 *
 * A single method backs both the `enable` and `disable` lifecycle actions:
 * @c enabled selects which one.
 */
struct SetLinkEnabledCommand
{
    ///< Slug of the link to toggle (required).
    std::string slug;

    ///< Target enabled state.
    bool enabled = true;
};

/**
 * @brief Restore command for a soft-deleted link.
 *
 * Clears @c deleted_at, mirroring the REST
 * `POST /api/v1/links/{slug}/restore` action.
 */
struct RestoreLinkCommand
{
    ///< Slug of the link to restore (required).
    std::string slug;
};

/**
 * @brief Query parameters for retrieving aggregate click-event
 * statistics for a single link.
 */
struct GetLinkStatsQuery
{
    ///< Slug of the link to retrieve statistics for.
    std::string slug;

    ///< Start of the time range; format is implementation-defined
    ///< (the legacy adapter expects a Unix epoch-second integer
    ///< string; `from` must be strictly less than `to`).
    std::string from;

    ///< End of the time range; format matches `from`.
    std::string to;

    ///< Aggregation bucket granularity; accepted values are
    ///< `"hour"`, `"day"`, and `"week"`.
    std::string bucket;
};

/**
 * @brief Per-HTTP-status-code redirect attempt count, one entry per
 * distinct status code in the queried time range.
 */
struct StatusCodeCountView
{
    ///< HTTP response status code (e.g. 301, 302, 404).
    uint16_t status_code = 0;

    ///< Number of redirect attempts that resulted in `status_code`.
    uint64_t count = 0;
};

/**
 * @brief Per-referring-domain redirect attempt count, one entry per
 * distinct domain in the queried time range.
 */
struct DomainCountView
{
    ///< Host header value from the inbound request, as sanitized by
    ///< `ClickEventSanitizer` before analytics storage.
    std::string domain;

    ///< Number of redirect attempts whose host header matched
    ///< `domain`.
    uint64_t count = 0;
};

/**
 * @brief Redirect attempt count for one time bucket, ordered by
 * `bucket_start_epoch` ascending in a `LinkStatsView`.
 */
struct TimeBucketView
{
    ///< Bucket start time as Unix epoch seconds; the bucket width
    ///< is determined by `GetLinkStatsQuery::bucket`.
    int64_t bucket_start_epoch = 0;

    ///< Number of redirect attempts that fell within this bucket.
    uint64_t count = 0;
};

/**
 * @brief Aggregate click-event statistics for one link over a
 * queried time range, populated by `ILinkStatsReader::read`.
 */
struct LinkStatsView
{
    ///< Slug of the queried link.
    std::string slug;

    ///< Total redirect attempts in the queried time range.
    uint64_t total_attempts = 0;

    ///< Redirect attempts that resulted in a successful redirect
    ///< response in the queried time range.
    uint64_t successful_redirects = 0;

    ///< Breakdown of attempt counts by HTTP response status code.
    std::vector<StatusCodeCountView> status_code_counts;

    ///< Breakdown of attempt counts by referring domain.
    std::vector<DomainCountView> domain_counts;

    ///< Attempt counts grouped by time bucket, ordered ascending
    ///< by `bucket_start_epoch`.
    std::vector<TimeBucketView> time_buckets;

    ///< Start of the queried time range; mirrored from
    ///< `GetLinkStatsQuery::from`.
    std::string from;

    ///< End of the queried time range; mirrored from
    ///< `GetLinkStatsQuery::to`.
    std::string to;

    ///< Aggregation bucket granularity used for `time_buckets`;
    ///< mirrored from `GetLinkStatsQuery::bucket`.
    std::string bucket;
};

/**
 * @brief Read-model projection of a `Link` record, returned by all
 * `LinkCommandService` query and command methods.
 *
 * Fields mirror the underlying `Link` struct but add `short_url`
 * (computed from `ServerConfig::shortener_base_domain`) and `status`
 * (derived via `resolveLinkStatus`).
 */
struct LinkView
{
    ///< Stable link identifier (UUID assigned at creation).
    std::string id;

    ///< Public short-code slug.
    std::string slug;

    ///< Normalized destination URL.
    std::string target_url;

    ///< Fully-qualified short URL, composed from
    ///< `ServerConfig::shortener_base_domain` and `slug`.
    std::string short_url;

    ///< ISO 8601 / RFC3339 timestamp of when the link was created.
    std::string created_at;

    ///< ISO 8601 / RFC3339 timestamp of the most recent mutation.
    std::string updated_at;

    ///< Optional expiry timestamp in ISO 8601 / RFC3339 format;
    ///< absent if the link does not expire.
    std::optional<std::string> expires_at;

    ///< Optional soft-delete timestamp; absent if the link has not
    ///< been soft-deleted.
    std::optional<std::string> deleted_at;

    ///< `true` when the link is enabled and may serve redirects
    ///< (subject to expiry and deletion state).
    bool enabled = true;

    ///< User-supplied tag list.
    std::vector<std::string> tags;

    ///< User-supplied flat string-to-string metadata map.
    std::unordered_map<std::string, std::string> metadata;

    ///< Optional UTM-style campaign attribution.
    std::optional<Link::Campaign> campaign;

    ///< Lifetime and rolling-window redirect counters (see
    ///< `Link::Stats`).
    Link::Stats stats;

    ///< HTTP redirect semantics: `temporary` (302) or
    ///< `permanent` (301).
    RedirectType redirect_type = RedirectType::temporary;

    ///< Computed lifecycle status derived from `enabled`,
    ///< `expires_at`, and `deleted_at` via `resolveLinkStatus`.
    LinkStatus status = LinkStatus::active;
};

/**
 * @brief Application-layer storage port for link persistence and
 * cache-aside reads, used exclusively by `LinkCommandService`.
 *
 * Methods report errors through the `bool` + `AppError*` out-param
 * convention or by returning `std::nullopt`; they never throw for
 * expected failure conditions. Implementations must translate
 * backend-specific error codes to `AppErrorCode` values.
 *
 * Thread-safety: implementations must be safe to call concurrently
 * from multiple threads, as `LinkCommandService` may be invoked from
 * different request-handling threads simultaneously.
 */
class ILinkStore
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed
     * through a base `ILinkStore*`.
     */
    virtual ~ILinkStore() = default;

    /**
     * @brief Persists a new link record.
     * @param link Link to store; `link.id` and `link.slug` must
     * already be set by the caller.
     * @param[out] error If non-null and the call fails, receives the
     * failure reason; left untouched on success.
     * @return `true` if the link was stored; `false` if the slug or
     * ID is already in use, or if the backend failed.
     */
    virtual bool create(const Link& link, AppError* error = nullptr) = 0;

    /**
     * @brief Looks up a link by its short-code slug.
     * @param slug Slug to look up.
     * @return The matching link; `std::nullopt` if not found.
     */
    virtual std::optional<Link> findBySlug(const std::string& slug) const = 0;

    /**
     * @brief Looks up a link by its stable identifier.
     * @param id Stable link identifier to look up.
     * @return The matching link; `std::nullopt` if not found.
     */
    virtual std::optional<Link> findById(const std::string& id) const = 0;

    /**
     * @brief Checks whether the given slug is registered.
     * @param slug Slug to check.
     * @return `true` if the slug exists; `false` otherwise.
     */
    virtual bool slugExists(const std::string& slug) const = 0;

    /**
     * @brief Invalidates any cached representation of `slug`.
     * Implementations that do not cache may treat this as a no-op.
     * @param slug Slug whose cache entry to evict.
     */
    virtual void invalidateCache(const std::string& slug) = 0;

    /**
     * @brief Persists a full-record update for an existing link and
     * invalidates any cached copy. Used by update/delete/lifecycle
     * commands, which load a link, mutate the relevant fields, and
     * write it back in one call.
     * @param link Updated link record; must already exist in the
     * store (looked up by `link.id` by the implementation).
     * @param[out] error If non-null and the call fails, receives the
     * failure reason (typically `AppErrorCode::storage_failure`, or
     * `AppErrorCode::not_found` if the record no longer exists);
     * left untouched on success.
     * @return `true` on success; `false` if the record does not
     * exist or the backend rejected the write.
     */
    virtual bool update(const Link& link, AppError* error = nullptr) = 0;
};

/**
 * @brief Application-layer read port for aggregate click-event
 * statistics, used by `LinkCommandService::GetLinkStats`.
 *
 * Implementations query the analytics storage backend and project
 * results into `LinkStatsView`. Thread-safety: implementations must
 * be safe to call concurrently from multiple threads.
 */
class ILinkStatsReader
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed
     * through a base `ILinkStatsReader*`.
     */
    virtual ~ILinkStatsReader() = default;

    /**
     * @brief Reads aggregate click-event statistics for the slug
     * and time range specified in `query`.
     * @param query Parameters for the stats query; see
     * `GetLinkStatsQuery` field documentation for validation rules
     * (empty slug, unparseable timestamps, unrecognised bucket, and
     * storage failures are common failure paths).
     * @return A populated `Result<LinkStatsView>` on success; a
     * `Result` carrying an appropriate `AppErrorCode` on failure.
     */
    virtual Result<LinkStatsView> read(
        const GetLinkStatsQuery& query) const = 0;
};

/**
 * @brief Coordinates all link management commands (create, update,
 * delete, enable/disable, restore) and queries (get, preview, stats)
 * for the application layer.
 *
 * Delegates persistence and cache-aside to `ILinkStore` and analytics
 * reads to `ILinkStatsReader`; applies field validation (URL, slug,
 * tags, metadata, campaign, timestamps) before writing.
 *
 * Thread-safety: all const methods are safe to call concurrently
 * provided `ILinkStore` and `ILinkStatsReader` implementations are
 * also thread-safe.
 *
 * Lifetime: does not own `store`, `stats_reader`, or `config`; the
 * caller (composition root) must keep all three alive for at least
 * as long as this object.
 */
class LinkCommandService
{
public:
    /**
     * @brief Stores references to `store`, `stats_reader`, and
     * `config`; does not start any background work.
     * @param store Link persistence and cache-aside port; must
     * outlive this object.
     * @param stats_reader Analytics read port; must outlive this
     * object.
     * @param config Server configuration supplying base domain,
     * slug length, default redirect type, and optional default
     * expiry; must outlive this object.
     */
    LinkCommandService(ILinkStore& store,
                       ILinkStatsReader& stats_reader,
                       const ServerConfig& config);

    /**
     * @brief Creates a new short link.
     * @param command Command with required `target_url` and optional
     * slug, redirect type, expiry, enabled state, tags, metadata,
     * and campaign.
     * @return `Result<LinkView>` populated on success; on failure:
     * `invalid_url` if `target_url` is not a valid absolute
     * HTTP/HTTPS URL or targets a disallowed private host;
     * `invalid_slug` if the custom slug fails format validation;
     * `reserved_slug` if the custom slug is system-reserved;
     * `slug_conflict` if the slug is already in use;
     * `invalid_field` if `expires_at` is not RFC3339 UTC, `tags`
     * violate constraints, `metadata` violates key/value limits, or
     * `campaign` fields exceed allowed lengths;
     * `internal` if the slug generator exhausted all attempts;
     * `storage_failure` if the repository call failed.
     */
    Result<LinkView> CreateLink(const CreateLinkCommand& command) const;

    /**
     * @brief Retrieves a link by slug or stable ID.
     * @param query Lookup discriminator and value.
     * @return `Result<LinkView>` on success; `not_found` if no link
     * matches the given slug or ID.
     */
    Result<LinkView> GetLink(const GetLinkQuery& query) const;

    /**
     * @brief Retrieves aggregate click-event statistics for a link.
     * @param query Stats query; delegates directly to
     * `ILinkStatsReader::read` without further transformation.
     * @return The result returned by `ILinkStatsReader::read`.
     */
    Result<LinkStatsView> GetLinkStats(const GetLinkStatsQuery& query) const;

    /**
     * @brief Apply a partial update to an existing link (PATCH
     * semantics).
     *
     * Reuses the same validators (`validateTags`/`validateMetadata`/
     * `validateCampaign`) and RFC3339 parsing as link creation.
     * Absent fields are left untouched; an explicit-null
     * `expires_at`/`campaign` clears the value.
     * @param command Partial-update command; see `UpdateLinkCommand`
     * field documentation for per-field PATCH semantics.
     * @return `Result<LinkView>` reflecting the updated state on
     * success; `not_found` if the slug does not exist;
     * `invalid_field` if any present field fails validation;
     * `storage_failure` if the repository write failed.
     */
    Result<LinkView> UpdateLink(const UpdateLinkCommand& command) const;

    /**
     * @brief Soft-delete an existing link by setting `deleted_at`.
     * @param command Command identifying the slug to soft-delete.
     * @return `Result<LinkView>` reflecting the post-delete state on
     * success; `not_found` if the slug does not exist;
     * `storage_failure` if the repository write failed.
     */
    Result<LinkView> DeleteLink(const DeleteLinkCommand& command) const;

    /**
     * @brief Enable or disable an existing link.
     * @param command Command identifying the slug and target enabled
     * state.
     * @return `Result<LinkView>` reflecting the updated state on
     * success; `not_found` if the slug does not exist;
     * `storage_failure` if the repository write failed.
     */
    Result<LinkView> SetLinkEnabled(const SetLinkEnabledCommand& command) const;

    /**
     * @brief Restore a soft-deleted link by clearing `deleted_at`.
     * @param command Command identifying the slug to restore.
     * @return `Result<LinkView>` reflecting the restored state on
     * success; `not_found` if the slug does not exist (whether or
     * not it was previously soft-deleted);
     * `storage_failure` if the repository write failed.
     */
    Result<LinkView> RestoreLink(const RestoreLinkCommand& command) const;

    /**
     * @brief Look up a link for preview without mutating it.
     *
     * Returns the same `LinkView` shape as `GetLink`; the transport
     * layer projects it onto the reduced preview response via
     * `serializeLinkPreviewJson`.
     * @param query Lookup discriminator and value (slug or ID).
     * @return `Result<LinkView>` on success; `not_found` if the
     * link does not exist.
     */
    Result<LinkView> PreviewLink(const GetLinkQuery& query) const;

private:
    /**
     * @brief Validates or generates the slug for a `CreateLink` call.
     *
     * If `command.slug` is set, validates format, checks for
     * system-reserved names, and verifies uniqueness. If absent,
     * randomly generates up to 10 candidates of
     * `config_.shortener_generated_slug_length` characters.
     * @param command The create command whose `slug` field is used
     * when present; a slug is generated otherwise.
     * @return `Result<std::string>` with the chosen slug on success;
     * `invalid_slug` if the custom slug fails format checks;
     * `reserved_slug` if the custom slug is system-reserved;
     * `slug_conflict` if the custom slug is already in use;
     * `internal` if generation exhausted all 10 candidates.
     */
    Result<std::string> chooseSlug(const CreateLinkCommand& command) const;

    /**
     * @brief Projects a `Link` record into a `LinkView`, computing
     * `short_url` from `config_` and `status` via
     * `resolveLinkStatus`.
     * @param link Link record to project.
     * @return The corresponding `LinkView`.
     */
    LinkView toView(const Link& link) const;

    ///< Link persistence and cache-aside port; not owned, must
    ///< outlive this object.
    ILinkStore& store_;

    ///< Analytics read port; not owned, must outlive this object.
    ILinkStatsReader& stats_reader_;

    ///< Server configuration (base domain, slug length, default
    ///< redirect type, default expiry); not owned, must outlive
    ///< this object.
    const ServerConfig& config_;
};

/**
 * @brief Serializes a `LinkView` to its full JSON representation.
 *
 * Emits all link fields: `id`, `slug`, `url`, `short_url`,
 * `created_at`, `updated_at`, `status`, `redirect_type`, `enabled`,
 * `tags`, `metadata` (keys sorted lexicographically), `campaign`,
 * and `stats`. For the reduced preview projection, see
 * `serializeLinkPreviewJson`.
 * @param link Fully populated view to serialize.
 * @return JSON object string with all link fields.
 */
std::string serializeLinkViewJson(const LinkView& link);

/**
 * @brief Serialize the reduced preview projection of a link to JSON.
 *
 * Emits exactly the fields the `GET /api/v1/links/{slug}/preview` REST endpoint
 * returns, in this order: `slug`, `url`, `status`, `redirect_type`, `enabled`,
 * `expires_at`, `deleted_at`. This is the operator's safety-check shape (is the
 * link enabled, expired, or soft-deleted?) and deliberately omits the `id`,
 * `short_url`, timestamps, tags, metadata, campaign, and stats that
 * @ref serializeLinkViewJson carries. Shared verbatim by the REST preview
 * handler and the CLI `link preview` verb so both stay byte-for-byte identical.
 *
 * @param link Fully populated view returned by @ref
 * LinkCommandService::PreviewLink.
 * @return The canonical reduced-preview JSON object for @p link.
 */
std::string serializeLinkPreviewJson(const LinkView& link);
/**
 * @brief Serializes a `LinkStatsView` to its JSON representation.
 *
 * Emits: `slug`, `total_attempts`, `successful_redirects`,
 * `attempts_by_status_code` (object: status-code key to count),
 * `attempts_by_domain` (object: domain to count), `time_buckets`
 * (array of `{"bucket_start": N, "count": N}` objects), `from`,
 * `to`, `bucket`. The `from` and `to` values are written without
 * quoting, so they appear as bare JSON numbers when they are
 * epoch-second integer strings (the format produced by the legacy
 * stats reader).
 * @param stats Populated stats view to serialize.
 * @return JSON object string with all stats fields.
 */
std::string serializeLinkStatsJson(const LinkStatsView& stats);

}  // namespace app
}  // namespace url_shortener

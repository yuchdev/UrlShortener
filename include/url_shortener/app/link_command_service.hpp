#pragma once

#include <url_shortener/app/app_error.hpp>
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
    std::string slug;  ///< Slug of the link to update (required).
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
    std::string slug;  ///< Slug of the link to soft-delete (required).
};

/**
 * @brief Enable/disable command for an existing link.
 *
 * A single method backs both the `enable` and `disable` lifecycle actions:
 * @c enabled selects which one.
 */
struct SetLinkEnabledCommand
{
    std::string slug;  ///< Slug of the link to toggle (required).
    bool enabled = true;  ///< Target enabled state.
};

/**
 * @brief Restore command for a soft-deleted link.
 *
 * Clears @c deleted_at, mirroring the REST
 * `POST /api/v1/links/{slug}/restore` action.
 */
struct RestoreLinkCommand
{
    std::string slug;  ///< Slug of the link to restore (required).
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
    /// Persist a full-record update for an already-existing link and invalidate
    /// any cached copy. Used by the update/delete/lifecycle commands, which
    /// load a link, mutate the relevant fields, and write it back in one call.
    virtual void update(const Link& link) = 0;
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

    /**
     * @brief Apply a partial update to an existing link (PATCH semantics).
     *
     * Reuses the same validators (`validateTags`/`validateMetadata`/
     * `validateCampaign`) and RFC3339 parsing as link creation. Absent fields
     * are left untouched; an explicit-null @c expires_at/@c campaign clears the
     * value. Returns @c not_found if the slug does not exist and
     * @c invalid_field for any field that fails validation.
     */
    Result<LinkView> UpdateLink(const UpdateLinkCommand& command) const;

    /**
     * @brief Soft-delete an existing link by setting @c deleted_at.
     *
     * Returns @c not_found if the slug does not exist.
     */
    Result<LinkView> DeleteLink(const DeleteLinkCommand& command) const;

    /**
     * @brief Enable or disable an existing link.
     *
     * Returns @c not_found if the slug does not exist.
     */
    Result<LinkView> SetLinkEnabled(const SetLinkEnabledCommand& command) const;

    /**
     * @brief Restore a soft-deleted link by clearing @c deleted_at.
     *
     * Returns @c not_found if the slug does not exist.
     */
    Result<LinkView> RestoreLink(const RestoreLinkCommand& command) const;

    /**
     * @brief Look up a link for preview without mutating it.
     *
     * Returns the same @ref LinkView shape as @ref GetLink; the transport layer
     * projects it onto the reduced preview response. Returns @c not_found if the
     * link does not exist.
     */
    Result<LinkView> PreviewLink(const GetLinkQuery& query) const;

private:
    Result<std::string> chooseSlug(const CreateLinkCommand& command) const;
    LinkView toView(const Link& link) const;

    ILinkStore& store_;
    ILinkStatsReader& stats_reader_;
    const ServerConfig& config_;
};

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
 * @param link Fully populated view returned by @ref LinkCommandService::PreviewLink.
 * @return The canonical reduced-preview JSON object for @p link.
 */
std::string serializeLinkPreviewJson(const LinkView& link);
std::string serializeLinkStatsJson(const LinkStatsView& stats);

} // namespace app
} // namespace url_shortener

#include <stdexcept>
#include <utility>

#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/storage/errors/cache_error.hpp>
#include <url_shortener/storage/errors/repo_error.hpp>
#include <url_shortener/storage/models/create_link_request.hpp>
#include <url_shortener/storage/models/link_record.hpp>

namespace url_shortener
{
namespace app
{
namespace
{

/**
 * @brief Logger component label used for every line this store emits.
 *
 * Kept as a single constant so the privacy rule (slug + error name
 * only, never values) is enforced at one place.
 */
constexpr char k_log_component[] = "app";

/**
 * @brief Stable error-name string for a `::RepoError`, safe to log.
 *
 * Mirrors `operator<<(std::ostream&, RepoError)` but without the
 * `RepoError::` prefix, so a failing read can be logged as a structured
 * field value. Never includes any record content.
 * @param e Repository error code to name.
 * @return A fixed, non-sensitive symbolic name (e.g. `"transient_failure"`).
 */
const char* repo_error_name(::RepoError e)
{
    switch (e) {
        case ::RepoError::none:
            return "none";
        case ::RepoError::not_found:
            return "not_found";
        case ::RepoError::already_exists:
            return "already_exists";
        case ::RepoError::invalid_argument:
            return "invalid_argument";
        case ::RepoError::transient_failure:
            return "transient_failure";
        case ::RepoError::permanent_failure:
            return "permanent_failure";
    }
    return "unknown";
}

/**
 * @brief Decodes a `::LinkRecord` into a `Link`, logging a slug-only
 * warning on failure.
 *
 * Wraps `record_to_link` so both read paths (`findBySlug`, `findById`)
 * share one fail-closed decode that never logs the (untrusted) record
 * content — only the slug and the codec error name (threat model
 * T-37).
 * @param record Record to decode (from any backend).
 * @param slug Slug to attribute a decode failure to in the log line.
 * @return The decoded link on success; `std::nullopt` when the codec
 * rejects the record, in which case `link_store_decode_failed` is
 * logged with the slug and the codec error name only.
 */
std::optional<Link> decode(const ::LinkRecord& record, const std::string& slug)
{
    Link link;
    std::string codec_error;
    if (record_to_link(record, &link, &codec_error)) {
        return link;
    }
    auto log = observability::LoggerFactory::Component(k_log_component);
    log.warn("link_store_decode_failed",
             {{"slug", slug}, {"error", codec_error}});
    return std::nullopt;
}

/**
 * @brief Translates a repository `::RepoError` into an `AppError`,
 * reusing the exact messages `LegacyLinkStore` emits.
 *
 * Shared by `create` and `update` so a backend swap leaves REST/CLI
 * error output byte-for-byte unchanged (contracts C3/C4). The two
 * callers never observe each other's codes in practice (`create`
 * cannot see `not_found`, `update` cannot see `already_exists`), so a
 * single mapping covers both without widening either contract.
 * @param e Non-`none` repository error to translate.
 * @param[out] error Destination; ignored when null. On return holds:
 * `slug_conflict` / "slug already in use" for `already_exists`;
 * `invalid_field` / "invalid link" for `invalid_argument`; `not_found`
 * / "Link not found" for `not_found`; `storage_failure` / "repository
 * failure" for every other value (including `transient_failure` and
 * `permanent_failure`).
 */
void map_repo_error(::RepoError e, AppError* error)
{
    if (error == nullptr) {
        return;
    }
    switch (e) {
        case ::RepoError::already_exists:
            *error =
                AppError {AppErrorCode::slug_conflict, "slug already in use"};
            return;
        case ::RepoError::invalid_argument:
            *error = AppError {AppErrorCode::invalid_field, "invalid link"};
            return;
        case ::RepoError::not_found:
            *error = AppError {AppErrorCode::not_found, "Link not found"};
            return;
        case ::RepoError::none:
        case ::RepoError::transient_failure:
        case ::RepoError::permanent_failure:
            break;
    }
    *error = AppError {AppErrorCode::storage_failure, "repository failure"};
}

/**
 * @brief Best-effort `Delete` on the configured cache-aside store.
 *
 * A no-op when `cache` is null. The `::ICacheStore::Delete` contract is
 * non-throwing, but a misbehaving adapter is still contained here: any
 * exception is swallowed so a cache failure can never fail the write
 * request (cache-aside contract, CLAUDE.md). The backend result is
 * intentionally ignored.
 * @param cache Configured cache-aside store, or null when none is
 * configured; not owned.
 * @param slug Short-code slug whose cached copy to evict.
 */
void best_effort_cache_delete(::ICacheStore* cache, const std::string& slug)
{
    if (cache == nullptr) {
        return;
    }
    try {
        ::CacheError cache_error = ::CacheError::none;
        (void)cache->Delete(slug, &cache_error);
    } catch (...) {
        // Cache failures never fail a request; nothing is logged because
        // the slug is already known to the caller and no value is exposed.
    }
}

}  // namespace

RepositoryLinkStore::RepositoryLinkStore(
    std::shared_ptr<::IMetadataRepository> repo,
    std::shared_ptr<::ICacheStore> cache,
    std::shared_ptr<url_shortener::ILinkCacheStore> l1,
    const IClock& clock,
    RepositoryLinkStoreOptions options)
    : m_repo_(std::move(repo))
    , m_cache_(std::move(cache))
    , m_l1_(std::move(l1))
    , m_clock_(clock)
    , m_options_(options)
{
    if (m_repo_ == nullptr) {
        throw std::invalid_argument(
            "RepositoryLinkStore: repo must not be null");
    }
    if (m_l1_ == nullptr) {
        throw std::invalid_argument("RepositoryLinkStore: l1 must not be null");
    }
    (void)m_clock_;
}

bool RepositoryLinkStore::create(const Link& link, AppError* error)
{
    // 1. Map to a create request through the codec (no re-validation of
    //    URL/slug happens here; that stays upstream, threat model T-40).
    ::CreateLinkRequest request;
    std::string codec_error;
    if (!link_to_create_request(link, &request, &codec_error)) {
        if (error != nullptr) {
            *error = AppError {AppErrorCode::internal, "invalid link record"};
        }
        return false;
    }

    // 2. The repositories store `now` as both created_at and updated_at;
    //    an unparseable created_at is a malformed record, not a backend
    //    failure, so fail before any repository call.
    const auto now = parseRfc3339Zulu(link.created_at);
    if (!now.has_value()) {
        if (error != nullptr) {
            *error = AppError {AppErrorCode::internal, "invalid link record"};
        }
        return false;
    }

    // 3. Persist. Uniqueness is enforced atomically by the repository.
    ::RepoError repo_error = ::RepoError::none;
    if (!m_repo_->CreateLink(request, link.id, *now, &repo_error)) {
        map_repo_error(repo_error, error);
        return false;
    }

    // 4. Populate L1 with a fresh copy so the creator's next read is a
    //    hit, and invalidate the configured cache best-effort.
    m_l1_->set(link.slug, link);
    best_effort_cache_delete(m_cache_.get(), link.slug);
    return true;
}

std::optional<Link> RepositoryLinkStore::findBySlug(
    const std::string& slug) const
{
    // 1. L1 hot-path cache: a hit is exactly one lookup, no repository
    //    I/O (redirect fast path, C10). The configured ::ICacheStore is
    //    never consulted for link data (threat model T-38).
    if (auto cached = m_l1_->get(slug)) {
        return cached;
    }

    // 2. Miss -> single authoritative repository read.
    ::RepoError repo_error = ::RepoError::none;
    std::optional<::LinkRecord> record =
        m_repo_->GetByShortCode(slug, &repo_error);

    if (!record.has_value()) {
        // An absent slug is not an error and is not cached (negative
        // results are never cached). A backend error is logged with the
        // error name only and also surfaces as nullopt (R-5 / T-35).
        if (repo_error != ::RepoError::none) {
            auto log = observability::LoggerFactory::Component(k_log_component);
            log.warn("link_store_read_failed",
                     {{"slug", slug}, {"error", repo_error_name(repo_error)}});
        }
        return std::nullopt;
    }

    // 3. Decode (fail-closed) and populate L1 on success.
    std::optional<Link> link = decode(*record, slug);
    if (link.has_value()) {
        m_l1_->set(slug, *link);
    }
    return link;
}

std::optional<Link> RepositoryLinkStore::findById(const std::string& id) const
{
    if (id.empty()) {
        return std::nullopt;
    }

    // Single indexed lookup: GetById returns inactive and soft-deleted
    // records too, so an admin lookup finds disabled/deleted links. The
    // former bounded ListLinks scan (threat model T-36) is gone.
    ::RepoError repo_error = ::RepoError::none;
    std::optional<::LinkRecord> record = m_repo_->GetById(id, &repo_error);

    if (!record.has_value()) {
        // An absent id is not an error and is not logged. A backend error
        // is logged with the id and error name only and also surfaces as
        // nullopt (R-5 / T-35).
        if (repo_error != ::RepoError::none) {
            auto log = observability::LoggerFactory::Component(k_log_component);
            log.warn("link_store_read_failed",
                     {{"id", id}, {"error", repo_error_name(repo_error)}});
        }
        return std::nullopt;
    }

    // Decode (fail-closed) and populate L1 on success.
    std::optional<Link> link = decode(*record, record->short_code);
    if (link.has_value()) {
        m_l1_->set(link->slug, *link);
    }
    return link;
}

bool RepositoryLinkStore::slugExists(const std::string& slug) const
{
    ::RepoError repo_error = ::RepoError::none;
    const bool exists = m_repo_->Exists(slug, &repo_error);
    if (repo_error != ::RepoError::none) {
        auto log = observability::LoggerFactory::Component(k_log_component);
        log.warn("link_store_exists_failed",
                 {{"slug", slug}, {"error", repo_error_name(repo_error)}});
        return false;
    }
    return exists;
}

void RepositoryLinkStore::invalidateCache(const std::string& slug)
{
    m_l1_->erase(slug);
    if (m_cache_ != nullptr) {
        // Best-effort invalidation of the configured cache-aside store:
        // its failure never fails a request (cache-aside contract).
        ::CacheError cache_error = ::CacheError::none;
        (void)m_cache_->Delete(slug, &cache_error);
    }
}

bool RepositoryLinkStore::update(const Link& link, AppError* error)
{
    // 1. Map to a full record through the codec (soft delete arrives
    //    here as a normal update with deleted_at set).
    ::LinkRecord record;
    std::string codec_error;
    if (!link_to_record(link, &record, &codec_error)) {
        if (error != nullptr) {
            *error = AppError {AppErrorCode::internal, "invalid link record"};
        }
        return false;
    }

    // 2. Persist the update.
    ::RepoError repo_error = ::RepoError::none;
    const bool ok = m_repo_->UpdateLink(record, &repo_error);

    // 3. Invalidate L1 whether the write succeeded or failed: a failed
    //    write leaves an uncertain state, so the next read must reload
    //    and never serve a value older than this write.
    m_l1_->erase(link.slug);
    if (!ok) {
        map_repo_error(repo_error, error);
        return false;
    }

    // 4. On success, also invalidate the configured cache best-effort.
    best_effort_cache_delete(m_cache_.get(), link.slug);
    return true;
}

bool RepositoryLinkStore::recordRedirect(const Link& /*link*/, AppError* error)
{
    if (error != nullptr) {
        *error = AppError {AppErrorCode::internal, "not implemented"};
    }
    return false;
}

}  // namespace app
}  // namespace url_shortener

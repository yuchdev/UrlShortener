#include <stdexcept>
#include <utility>

#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/storage/errors/repo_error.hpp>
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

bool RepositoryLinkStore::create(const Link& /*link*/, AppError* error)
{
    if (error != nullptr) {
        *error = AppError {AppErrorCode::internal, "not implemented"};
    }
    return false;
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

bool RepositoryLinkStore::update(const Link& /*link*/, AppError* error)
{
    if (error != nullptr) {
        *error = AppError {AppErrorCode::internal, "not implemented"};
    }
    return false;
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

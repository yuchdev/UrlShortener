/**
 * @file AppTestFakes.hpp
 * @brief Hand-written test doubles for app::ILinkStore and
 * app::ILinkStatsReader.
 *
 * Include this header in tests/unit/app/ unit tests. It provides:
 *   - FakeLinkStore  – in-memory ILinkStore with configurable failure
 * injection.
 *   - FakeLinkStatsReader – ILinkStatsReader that returns a canned view or a
 *     forced error.
 *   - makeTestConfig() – minimal ServerConfig for unit tests.
 *   - operator<< for AppErrorCode – required by BOOST_CHECK_EQUAL.
 */
#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <url_shortener/app/link_command_service.hpp>

// ---- Streaming operator for AppErrorCode (required by BOOST_CHECK_EQUAL) ----
namespace url_shortener
{
namespace app
{
inline std::ostream& operator<<(std::ostream& os, const AppErrorCode code)
{
    switch (code) {
        case AppErrorCode::none:
            os << "none";
            break;
        case AppErrorCode::invalid_url:
            os << "invalid_url";
            break;
        case AppErrorCode::invalid_slug:
            os << "invalid_slug";
            break;
        case AppErrorCode::reserved_slug:
            os << "reserved_slug";
            break;
        case AppErrorCode::slug_conflict:
            os << "slug_conflict";
            break;
        case AppErrorCode::invalid_field:
            os << "invalid_field";
            break;
        case AppErrorCode::not_found:
            os << "not_found";
            break;
        case AppErrorCode::storage_failure:
            os << "storage_failure";
            break;
        case AppErrorCode::internal:
            os << "internal";
            break;
        default:
            os << "unknown";
            break;
    }
    return os;
}
}  // namespace app
}  // namespace url_shortener

namespace test_fakes
{

/// Fake ILinkStore backed by two in-memory maps (by_slug, by_id).
/// All methods are thread-hostile – use only from a single test thread.
class FakeLinkStore final : public url_shortener::app::ILinkStore
{
public:
    // ------------------------------------------------------------------ //
    //  Configuration helpers (called from test setup before exercising)   //
    // ------------------------------------------------------------------ //

    /// Pretend this slug already exists without actually storing a link.
    /// Useful to force a slug_conflict via slugExists() without a prior
    /// create().
    void preseedSlugExists(const std::string& slug)
    {
        existing_slugs_.insert(slug);
    }

    /// When set, the next create() call returns a storage_failure error.
    void forceCreateStorageFailure(bool fail = true) { fail_create_ = fail; }

    /// Directly seed a Link into both maps (bypasses validation).
    void seed(const url_shortener::Link& link)
    {
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
    }

    // ------------------------------------------------------------------ //
    //  ILinkStore interface                                               //
    // ------------------------------------------------------------------ //

    bool create(const url_shortener::Link& link,
                url_shortener::app::AppError* err) override
    {
        if (fail_create_) {
            if (err != nullptr) {
                err->code = url_shortener::app::AppErrorCode::storage_failure;
                err->detail = "forced storage failure";
            }
            return false;
        }
        if (by_slug_.count(link.slug) || existing_slugs_.count(link.slug)) {
            if (err != nullptr) {
                err->code = url_shortener::app::AppErrorCode::slug_conflict;
                err->detail = "slug already in use";
            }
            return false;
        }
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
        return true;
    }

    std::optional<url_shortener::Link> findBySlug(
        const std::string& slug) const override
    {
        auto it = by_slug_.find(slug);
        if (it != by_slug_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<url_shortener::Link> findById(
        const std::string& id) const override
    {
        auto it = by_id_.find(id);
        if (it != by_id_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool slugExists(const std::string& slug) const override
    {
        return by_slug_.count(slug) > 0 || existing_slugs_.count(slug) > 0;
    }

    void invalidateCache(const std::string& /*slug*/) override {}

    /// Overwrite an existing link in both maps, keyed by slug and id.
    void update(const url_shortener::Link& link) override
    {
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
    }

private:
    std::unordered_map<std::string, url_shortener::Link> by_slug_;
    std::unordered_map<std::string, url_shortener::Link> by_id_;
    std::unordered_set<std::string> existing_slugs_;
    bool fail_create_ = false;
};

/// Fake ILinkStatsReader.
/// By default returns a minimal view echoing the query fields.
/// Call forceError() to inject an AppError that read() will return.
class FakeLinkStatsReader final : public url_shortener::app::ILinkStatsReader
{
public:
    void forceError(url_shortener::app::AppError err)
    {
        forced_error_ = std::move(err);
    }

    url_shortener::app::Result<url_shortener::app::LinkStatsView> read(
        const url_shortener::app::GetLinkStatsQuery& query) const override
    {
        if (forced_error_.has_value()) {
            return {std::nullopt, *forced_error_};
        }
        url_shortener::app::LinkStatsView view;
        view.slug = query.slug;
        view.from = query.from;
        view.to = query.to;
        view.bucket = query.bucket;
        return {view, {}};
    }

private:
    std::optional<url_shortener::app::AppError> forced_error_;
};

/// Minimal ServerConfig suitable for unit tests.
/// Does NOT set expiry or private-target overrides.
inline ServerConfig makeTestConfig()
{
    ServerConfig cfg;
    cfg.shortener_base_domain = "http://sho.rt";
    cfg.shortener_generated_slug_length = 7;
    cfg.shortener_default_redirect_type = "temporary";
    cfg.shortener_allow_private_targets = false;
    return cfg;
}

}  // namespace test_fakes

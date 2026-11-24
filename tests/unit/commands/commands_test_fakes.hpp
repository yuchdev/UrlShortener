/**
 * @file commands_test_fakes.hpp
 * @brief Hand-written test doubles for `app::ILinkStore` /
 * `app::ILinkStatsReader`, scoped to `tests/unit/commands/`.
 *
 * A local copy of the `tests/unit/app/app_test_fakes.hpp` doubles: that
 * directory is not on this test target's include path, so the command tests
 * carry their own fakes here rather than widening the include paths (subtask 08
 * dependency note). Adds what the `link.import` job body needs that the app
 * fakes lack: thread safety (the import body runs on a worker while `link.get`
 * reads from the request thread) and a blocking `on_create` hook for the
 * head-of-line and cancel-mid-import tests.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <url_shortener/app/link_command_service.hpp>

namespace commands_test_fakes
{

/**
 * @brief Thread-safe in-memory `app::ILinkStore` with a blocking create hook.
 *
 * Backed by two maps guarded by one mutex, so a worker-thread `create` and a
 * request-thread `findBySlug`/`slugExists` may run concurrently. The optional
 * @ref on_create hook is invoked with the 1-based create-attempt count **before
 * the mutex is taken**, so a test that blocks inside it does not hold the lock
 * and therefore does not head-of-line-block concurrent reads.
 */
class FakeLinkStore final : public url_shortener::app::ILinkStore
{
public:
    ///< Optional hook called at the start of every @ref create with the
    ///< 1-based attempt count, before the store mutex is acquired. A test may
    ///< block here (e.g. on a latch) to gate the import mid-stream.
    std::function<void(std::size_t)> on_create;

    /// Pretend @p slug already exists, forcing a `slug_conflict` on create
    /// without a prior successful create.
    void preseed_slug(const std::string& slug)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        existing_slugs_.insert(slug);
    }

    /// The number of @ref create calls made so far (including conflicts).
    std::size_t create_calls() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return create_calls_;
    }

    /// The number of @ref update calls made so far. `link.update`,
    /// `link.delete`, `link.enable`, `link.disable` and `link.restore` all
    /// persist through @ref update (`ILinkStore` has no distinct delete method;
    /// soft-delete and lifecycle changes are writes through `update`), so this
    /// counts every mutating repository touch other than @ref create. A denial
    /// proof asserts both counters stay at their pre-denial value.
    std::size_t update_calls() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return update_calls_;
    }

    /// The number of links currently stored.
    std::size_t size() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return by_slug_.size();
    }

    /// A snapshot copy of every stored link, for assertions.
    std::vector<url_shortener::Link> all() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        std::vector<url_shortener::Link> out;
        out.reserve(by_slug_.size());
        for (const auto& entry : by_slug_) {
            out.push_back(entry.second);
        }
        return out;
    }

    bool create(const url_shortener::Link& link,
                url_shortener::app::AppError* error) override
    {
        std::size_t attempt = 0;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            attempt = ++create_calls_;
        }
        if (on_create) {
            on_create(attempt);  // may block; no lock held
        }

        std::lock_guard<std::mutex> guard(mutex_);
        if (by_slug_.count(link.slug) != 0
            || existing_slugs_.count(link.slug) != 0)
        {
            if (error != nullptr) {
                error->code = url_shortener::app::AppErrorCode::slug_conflict;
                error->detail = "slug already in use";
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
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = by_slug_.find(slug);
        if (it != by_slug_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<url_shortener::Link> findById(
        const std::string& id) const override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = by_id_.find(id);
        if (it != by_id_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool slugExists(const std::string& slug) const override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return by_slug_.count(slug) != 0 || existing_slugs_.count(slug) != 0;
    }

    void invalidateCache(const std::string& /*slug*/) override {}

    bool update(const url_shortener::Link& link,
                url_shortener::app::AppError* /*error*/ = nullptr) override
    {
        std::lock_guard<std::mutex> guard(mutex_);
        ++update_calls_;
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
        return true;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, url_shortener::Link> by_slug_;
    std::unordered_map<std::string, url_shortener::Link> by_id_;
    std::unordered_set<std::string> existing_slugs_;
    std::size_t create_calls_ = 0;
    std::size_t update_calls_ = 0;
};

/**
 * @brief Minimal `app::ILinkStatsReader` returning a view echoing the query.
 *
 * `link.import` never reads stats; this exists only to satisfy the
 * `LinkCommandService` constructor.
 */
class FakeStatsReader final : public url_shortener::app::ILinkStatsReader
{
public:
    url_shortener::app::Result<url_shortener::app::LinkStatsView> read(
        const url_shortener::app::GetLinkStatsQuery& query) const override
    {
        url_shortener::app::LinkStatsView view;
        view.slug = query.slug;
        view.from = query.from;
        view.to = query.to;
        view.bucket = query.bucket;
        return {view, {}};
    }
};

/**
 * @brief Minimal `ServerConfig` for the import tests.
 *
 * @param allow_private_targets Value for
 * `ServerConfig::shortener_allow_private_targets` (the SSRF switch the import
 * honours through `CreateLink`).
 * @return A config with a valid base domain and a 7-character generated slug
 * length.
 */
inline ServerConfig make_test_config(bool allow_private_targets = false)
{
    ServerConfig config;
    config.shortener_base_domain = "http://sho.rt";
    config.shortener_generated_slug_length = 7;
    config.shortener_default_redirect_type = "temporary";
    config.shortener_allow_private_targets = allow_private_targets;
    return config;
}

}  // namespace commands_test_fakes

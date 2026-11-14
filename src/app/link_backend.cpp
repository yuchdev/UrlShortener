#include <atomic>
#include <mutex>
#include <utility>

#include <url_shortener/app/legacy_adapters.hpp>
#include <url_shortener/app/link_backend.hpp>

namespace url_shortener
{
namespace app
{

namespace
{

/**
 * @brief Lock-free published pointer to the active store, or `nullptr`
 * when nothing is installed (the `LegacyLinkStore` default is used).
 *
 * Read by `store()`/`installed()` with acquire ordering; written by
 * `install()`/`reset()` under @ref s_install_mutex with release ordering.
 */
std::atomic<ILinkStore*> s_current {nullptr};

/**
 * @brief Owning reference that keeps the installed store alive while it is
 * installed. Non-null iff a non-null store is installed.
 *
 * Only touched under @ref s_install_mutex; never read on the hot path.
 */
std::shared_ptr<ILinkStore> s_owner;

/**
 * @brief Guards @ref s_owner and the publish/retire of @ref s_current for
 * `install()`/`reset()` only. The read side (`store()`) never takes it.
 */
std::mutex s_install_mutex;

}  // namespace

ILinkStore& LinkBackend::store()
{
    ILinkStore* const current = s_current.load(std::memory_order_acquire);
    if (current != nullptr) {
        return *current;
    }

    // Default: the stateless legacy bridge over the global
    // linkRepository()/linkCache() singletons. A function-local static so
    // it is constructed at most once, lazily, and never when a store is
    // installed. After first construction the magic-static guard is a
    // lock-free acquiring load, so the default path adds no lock.
    static LegacyLinkStore s_default;
    return s_default;
}

void LinkBackend::install(std::shared_ptr<ILinkStore> store)
{
    std::lock_guard<std::mutex> lock(s_install_mutex);
    // Store the owner first, then publish the raw pointer with release
    // ordering so a concurrent acquiring store() never sees a dangling or
    // half-constructed pointer.
    s_owner = std::move(store);
    s_current.store(s_owner.get(), std::memory_order_release);
}

void LinkBackend::reset()
{
    std::lock_guard<std::mutex> lock(s_install_mutex);
    // Retire the published pointer before dropping ownership so later
    // readers fall back to the default; in-flight readers that already
    // loaded the pointer are the caller's responsibility (teardown-only
    // contract, threat model T-39).
    s_current.store(nullptr, std::memory_order_release);
    s_owner.reset();
}

bool LinkBackend::installed()
{
    return s_current.load(std::memory_order_acquire) != nullptr;
}

}  // namespace app
}  // namespace url_shortener

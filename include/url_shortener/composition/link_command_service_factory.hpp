#pragma once

#include <memory>

#include <url_shortener/app/legacy_adapters.hpp>
#include <url_shortener/app/link_command_service.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief Ownership bundle returned by `BuildLegacyLinkCommandService`.
 *
 * Keeps the active link store and the `LinkCommandService` alive
 * together. The `service` member holds references into `store` and
 * `stats_reader`, so the bundle must be kept intact as a unit; do not
 * release `store` or `stats_reader` while `service` is still in use.
 */
struct LinkCommandServiceBundle
{
    ///< The active link store from `LinkBackend::store()` (the
    ///< installed store, or the `LegacyLinkStore` default). This is a
    ///< **non-owning** aliasing pointer with an empty control block: it
    ///< keeps `service`'s `ILinkStore&` valid for as long as the holder
    ///< keeps the store installed, but copying or destroying the bundle
    ///< never destroys the installed store (the holder owns it). Despite
    ///< the "Legacy" name, this now resolves to the current backend.
    std::shared_ptr<ILinkStore> store;

    ///< Legacy analytics stats reader; owned by this bundle and
    ///< must outlive `service`.
    std::shared_ptr<LegacyLinkStatsReader> stats_reader;

    ///< Fully-wired application service referencing `store` and
    ///< `stats_reader` by reference.
    std::shared_ptr<LinkCommandService> service;
};

/**
 * @brief Constructs a `LinkCommandService` wired to the active link
 * store.
 *
 * Binds `store` to `app::LinkBackend::store()` (the installed store, or
 * the `LegacyLinkStore` default when nothing is installed) via a
 * non-owning aliasing `std::shared_ptr`, creates a
 * `LegacyLinkStatsReader`, then constructs a `LinkCommandService` that
 * holds references to both. The name is retained because existing tests
 * and production call sites use it; it no longer implies the legacy
 * store specifically — it binds to whatever backend the holder has
 * installed.
 *
 * The returned bundle owns the `LinkCommandService` and the
 * `LegacyLinkStatsReader`; it does **not** own the store (the holder
 * does). The `service` member stores a reference to `config`, so
 * `config` must outlive the returned bundle, and the bundle must not
 * outlive the holder's installed store.
 * @param config Server configuration passed by reference to the
 * constructed `LinkCommandService`; must outlive the returned bundle.
 * @return A `LinkCommandServiceBundle` wired to the active store and
 * ready for use.
 */
LinkCommandServiceBundle BuildLegacyLinkCommandService(
    const ServerConfig& config);

}  // namespace app
}  // namespace url_shortener

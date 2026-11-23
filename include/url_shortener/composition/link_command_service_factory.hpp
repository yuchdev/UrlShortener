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
 * Keeps the legacy storage adapters and the `LinkCommandService`
 * alive together. The `service` member holds references into `store`
 * and `stats_reader`, so the bundle must be kept intact as a unit;
 * do not release `store` or `stats_reader` while `service` is still
 * in use.
 */
struct LinkCommandServiceBundle
{
    ///< Legacy link persistence and cache-aside adapter; owned by
    ///< this bundle and must outlive `service`.
    std::shared_ptr<LegacyLinkStore> store;

    ///< Legacy analytics stats reader; owned by this bundle and
    ///< must outlive `service`.
    std::shared_ptr<LegacyLinkStatsReader> stats_reader;

    ///< Fully-wired application service referencing `store` and
    ///< `stats_reader` by reference.
    std::shared_ptr<LinkCommandService> service;
};

/**
 * @brief Constructs a `LinkCommandService` wired to the legacy
 * in-process storage adapters.
 *
 * Creates `LegacyLinkStore` and `LegacyLinkStatsReader`, then
 * constructs a `LinkCommandService` that holds references to them.
 * The returned bundle owns all three objects; the `service` member
 * stores a reference to `config`, so `config` must outlive the
 * returned bundle.
 * @param config Server configuration passed by reference to the
 * constructed `LinkCommandService`; must outlive the returned bundle.
 * @return A `LinkCommandServiceBundle` with all three objects
 * co-owned and ready for use.
 */
LinkCommandServiceBundle BuildLegacyLinkCommandService(
    const ServerConfig& config);

}  // namespace app
}  // namespace url_shortener

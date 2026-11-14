#include <memory>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/composition/link_command_service_factory.hpp>

namespace url_shortener
{
namespace app
{

LinkCommandServiceBundle BuildLegacyLinkCommandService(
    const ServerConfig& config)
{
    LinkCommandServiceBundle bundle;
    // Non-owning aliasing shared_ptr (empty control block): points at the
    // store the holder has installed (or the LegacyLinkStore default) and
    // stays valid while the holder keeps it installed, but never destroys
    // it when the bundle is copied or destroyed.
    bundle.store = std::shared_ptr<ILinkStore>(std::shared_ptr<void>(),
                                               &LinkBackend::store());
    bundle.stats_reader = std::make_shared<LegacyLinkStatsReader>();
    bundle.service = std::make_shared<LinkCommandService>(
        *bundle.store, *bundle.stats_reader, config);
    return bundle;
}

}  // namespace app
}  // namespace url_shortener

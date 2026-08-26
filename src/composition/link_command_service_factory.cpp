#include <url_shortener/composition/link_command_service_factory.hpp>

namespace url_shortener {
namespace app {

LinkCommandServiceBundle BuildLegacyLinkCommandService(const ServerConfig& config)
{
    LinkCommandServiceBundle bundle;
    bundle.store = std::make_shared<LegacyLinkStore>();
    bundle.stats_reader = std::make_shared<LegacyLinkStatsReader>();
    bundle.service = std::make_shared<LinkCommandService>(
        *bundle.store,
        *bundle.stats_reader,
        config);
    return bundle;
}

} // namespace app
} // namespace url_shortener

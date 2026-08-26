#pragma once

#include <url_shortener/app/legacy_adapters.hpp>
#include <url_shortener/app/link_command_service.hpp>

#include <memory>

namespace url_shortener {
namespace app {

struct LinkCommandServiceBundle
{
    std::shared_ptr<LegacyLinkStore> store;
    std::shared_ptr<LegacyLinkStatsReader> stats_reader;
    std::shared_ptr<LinkCommandService> service;
};

LinkCommandServiceBundle BuildLegacyLinkCommandService(const ServerConfig& config);

} // namespace app
} // namespace url_shortener

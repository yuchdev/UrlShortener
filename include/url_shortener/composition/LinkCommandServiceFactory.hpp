#pragma once

#include <url_shortener/app/LegacyAdapters.hpp>
#include <url_shortener/app/LinkCommandService.hpp>

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

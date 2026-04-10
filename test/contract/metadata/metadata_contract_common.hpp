#pragma once

#include <memory>

#include <boost/test/unit_test.hpp>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"

struct MetadataHarness {
    std::shared_ptr<ManualClock> clock;
    std::shared_ptr<IMetadataRepository> repo;
};

inline MetadataHarness MakeMetadataHarness()
{
    auto clock = std::make_shared<ManualClock>(std::chrono::system_clock::time_point{});
    auto repo = std::make_shared<InMemoryMetadataRepository>();
    return {clock, repo};
}

inline CreateLinkRequest BasicRequest(const std::string& code, const std::string& target = "https://example.com")
{
    CreateLinkRequest req;
    req.short_code = code;
    req.target_url = target;
    return req;
}

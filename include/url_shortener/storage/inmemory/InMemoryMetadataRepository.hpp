#pragma once

#include <shared_mutex>
#include <unordered_map>

#include "url_shortener/storage/IMetadataRepository.hpp"

/**
 * @brief Thread-safe in-memory metadata repository implementation.
 */
class InMemoryMetadataRepository final : public IMetadataRepository {
public:
    bool CreateLink(const CreateLinkRequest& request,
                    const std::string& id,
                    const std::chrono::system_clock::time_point& now,
                    RepoError* error = nullptr) override;
    std::optional<LinkRecord> GetByShortCode(const std::string& short_code,
                                             RepoError* error = nullptr) const override;
    bool UpdateLink(const LinkRecord& record, RepoError* error = nullptr) override;
    bool DeleteLink(const std::string& short_code, RepoError* error = nullptr) override;
    std::vector<LinkRecord> ListLinks(const ListLinksQuery& query, RepoError* error = nullptr) const override;
    bool Exists(const std::string& short_code, RepoError* error = nullptr) const override;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, LinkRecord> by_code_;
};

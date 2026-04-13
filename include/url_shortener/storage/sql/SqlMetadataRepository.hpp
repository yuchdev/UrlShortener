#pragma once

#include <memory>
#include <mutex>

#include "url_shortener/storage/IMetadataRepository.hpp"
#include "url_shortener/storage/sql/ISqlSessionFactory.hpp"
#include "url_shortener/storage/sql/SqlDialect.hpp"
#include "url_shortener/storage/sql/SqlMetadataRowMapper.hpp"

/**
 * @brief SQL-backed implementation of IMetadataRepository.
 */
class SqlMetadataRepository final : public IMetadataRepository {
public:
    SqlMetadataRepository(std::shared_ptr<ISqlSessionFactory> session_factory,
                          std::shared_ptr<SqlDialect> dialect,
                          std::shared_ptr<SqlMetadataRowMapper> mapper);

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
    bool EnsureBootstrapped(ISqlSession& session, RepoError* error) const;

    std::shared_ptr<ISqlSessionFactory> session_factory_;
    std::shared_ptr<SqlDialect> dialect_;
    std::shared_ptr<SqlMetadataRowMapper> mapper_;
    mutable std::mutex bootstrap_mutex_;
    mutable bool bootstrapped_ = false;
};

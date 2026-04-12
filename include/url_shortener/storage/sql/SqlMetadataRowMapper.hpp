#pragma once

#include "url_shortener/storage/models/CreateLinkRequest.hpp"
#include "url_shortener/storage/models/LinkRecord.hpp"
#include "url_shortener/storage/sql/ISqlSessionFactory.hpp"

/**
 * @brief Converts between LinkRecord domain model and SQL row model.
 */
class SqlMetadataRowMapper {
public:
    SqlRow ToCreateRow(const CreateLinkRequest& request,
                       const std::string& id,
                       const std::chrono::system_clock::time_point& now) const;
    SqlRow ToUpdateRow(const LinkRecord& record) const;
    bool FromRow(const SqlRow& row, LinkRecord* out, RepoError* error) const;
};

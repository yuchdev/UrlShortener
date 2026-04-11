#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "url_shortener/storage/errors/RepoError.hpp"
#include "url_shortener/storage/models/CreateLinkRequest.hpp"
#include "url_shortener/storage/models/LinkRecord.hpp"
#include "url_shortener/storage/models/ListLinksQuery.hpp"

/**
 * @brief Metadata repository abstraction for short-link lifecycle operations.
 */
class IMetadataRepository {
public:
    virtual ~IMetadataRepository() = default;

    /**
     * @brief Creates a new record.
     */
    virtual bool CreateLink(const CreateLinkRequest& request,
                            const std::string& id,
                            const std::chrono::system_clock::time_point& now,
                            RepoError* error = nullptr) = 0;

    /**
     * @brief Gets a record by public short code.
     */
    virtual std::optional<LinkRecord> GetByShortCode(const std::string& short_code,
                                                     RepoError* error = nullptr) const = 0;

    /**
     * @brief Updates mutable fields on an existing record.
     */
    virtual bool UpdateLink(const LinkRecord& record, RepoError* error = nullptr) = 0;

    /**
     * @brief Deletes an existing record by short code.
     */
    virtual bool DeleteLink(const std::string& short_code, RepoError* error = nullptr) = 0;

    /**
     * @brief Lists records by a query shape.
     */
    virtual std::vector<LinkRecord> ListLinks(const ListLinksQuery& query, RepoError* error = nullptr) const = 0;

    /**
     * @brief Checks if a short code exists.
     */
    virtual bool Exists(const std::string& short_code, RepoError* error = nullptr) const = 0;
};

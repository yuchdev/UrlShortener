#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "url_shortener/storage/errors/RepoError.hpp"

/**
 * @brief Row model for backend-neutral SQL session/query operations.
 */
struct SqlRow {
    std::optional<std::string> id;
    std::optional<std::string> short_code;
    std::optional<std::string> target_url;
    std::optional<long long> created_at;
    std::optional<long long> updated_at;
    std::optional<long long> expires_at;
    std::optional<int> is_active;
    std::optional<std::string> owner_id;
    std::optional<std::string> attributes_json;
};

/**
 * @brief Minimal backend-neutral SQL session operations used by metadata repository.
 */
class ISqlSession {
public:
    virtual ~ISqlSession() = default;

    virtual bool Bootstrap(const std::string& sql, RepoError* error) = 0;
    virtual bool InsertLink(const std::string& sql, const SqlRow& row, RepoError* error) = 0;
    virtual std::optional<SqlRow> SelectByShortCode(const std::string& sql,
                                                    const std::string& short_code,
                                                    RepoError* error) = 0;
    virtual bool UpdateLink(const std::string& sql, const SqlRow& row, RepoError* error) = 0;
    virtual bool DeleteLink(const std::string& sql, const std::string& short_code, RepoError* error) = 0;
    virtual bool Exists(const std::string& sql, const std::string& short_code, bool* exists, RepoError* error) = 0;
    virtual std::vector<SqlRow> List(const std::string& sql,
                                     const std::optional<std::string>& owner,
                                     bool include_inactive,
                                     std::size_t limit,
                                     std::size_t offset,
                                     RepoError* error) = 0;
};

/**
 * @brief Factory interface for creating configured SQL sessions.
 */
class ISqlSessionFactory {
public:
    virtual ~ISqlSessionFactory() = default;
    virtual std::unique_ptr<ISqlSession> Create(RepoError* error) const = 0;
};

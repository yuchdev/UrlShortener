#include "url_shortener/storage/sqlite/SqliteSqlDialect.hpp"

std::string SqliteSqlDialect::BootstrapSchemaSql() const
{
    return R"SQL(
CREATE TABLE IF NOT EXISTS links (
    id TEXT NOT NULL,
    short_code TEXT PRIMARY KEY,
    target_url TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    expires_at INTEGER,
    is_active INTEGER NOT NULL,
    owner_id TEXT,
    attributes_json TEXT
);
)SQL";
}

std::string SqliteSqlDialect::InsertSql() const
{
    return "INSERT INTO links(id, short_code, target_url, created_at, updated_at, expires_at, is_active, owner_id, attributes_json) "
           "VALUES(:id,:short_code,:target_url,:created_at,:updated_at,:expires_at,:is_active,:owner_id,:attributes_json);";
}

std::string SqliteSqlDialect::SelectByShortCodeSql() const
{
    return "SELECT id, short_code, target_url, created_at, updated_at, expires_at, is_active, owner_id, attributes_json "
           "FROM links WHERE short_code = :short_code;";
}

std::string SqliteSqlDialect::UpdateSql() const
{
    return "UPDATE links SET target_url = :target_url, updated_at = :updated_at, expires_at = :expires_at, "
           "is_active = :is_active, owner_id = :owner_id, attributes_json = :attributes_json WHERE short_code = :short_code;";
}

std::string SqliteSqlDialect::DeleteSql() const
{
    return "DELETE FROM links WHERE short_code = :short_code;";
}

std::string SqliteSqlDialect::ExistsSql() const
{
    return "SELECT COUNT(1) FROM links WHERE short_code = :short_code;";
}

std::string SqliteSqlDialect::ListSql() const
{
    return "SELECT id, short_code, target_url, created_at, updated_at, expires_at, is_active, owner_id, attributes_json FROM links "
           "WHERE (:owner_id IS NULL OR owner_id = :owner_id) "
           "AND (:include_inactive = 1 OR is_active = 1) "
           "ORDER BY created_at LIMIT :limit OFFSET :offset;";
}

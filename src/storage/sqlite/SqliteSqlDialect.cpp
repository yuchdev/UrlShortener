#include "url_shortener/storage/sqlite/SqliteSqlDialect.hpp"

#include <fstream>
#include <sstream>

std::string SqliteSqlDialect::BootstrapSchemaSql() const
{
    std::ifstream file("src/storage/sqlite/sql/001_init_links.sql");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
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

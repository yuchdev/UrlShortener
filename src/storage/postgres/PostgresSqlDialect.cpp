#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

std::string PostgresSqlDialect::BootstrapSchemaSql() const
{
    return R"SQL(
CREATE TABLE IF NOT EXISTS links (
    id VARCHAR NOT NULL,
    short_code VARCHAR PRIMARY KEY,
    target_url TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL,
    expires_at TIMESTAMPTZ NULL,
    is_active BOOLEAN NOT NULL,
    owner_id VARCHAR NULL,
    attributes_json JSONB NULL
);
)SQL";
}

std::string PostgresSqlDialect::InsertSql() const
{
    return "INSERT INTO links(id, short_code, target_url, created_at, updated_at, expires_at, is_active, owner_id, attributes_json) "
           "VALUES(:id,:short_code,:target_url,to_timestamp(:created_at),to_timestamp(:updated_at),to_timestamp(:expires_at),"
           "(:is_active <> 0),:owner_id,CAST(:attributes_json AS JSONB));";
}

std::string PostgresSqlDialect::SelectByShortCodeSql() const
{
    return "SELECT id, short_code, target_url, "
           "CAST(EXTRACT(EPOCH FROM created_at) AS BIGINT) AS created_at, "
           "CAST(EXTRACT(EPOCH FROM updated_at) AS BIGINT) AS updated_at, "
           "CASE WHEN expires_at IS NULL THEN NULL ELSE CAST(EXTRACT(EPOCH FROM expires_at) AS BIGINT) END AS expires_at, "
           "CASE WHEN is_active THEN 1 ELSE 0 END AS is_active, "
           "owner_id, CASE WHEN attributes_json IS NULL THEN NULL ELSE attributes_json::text END AS attributes_json "
           "FROM links WHERE short_code = :short_code;";
}

std::string PostgresSqlDialect::UpdateSql() const
{
    return "UPDATE links SET target_url = :target_url, updated_at = to_timestamp(:updated_at), "
           "expires_at = to_timestamp(:expires_at), is_active = (:is_active <> 0), owner_id = :owner_id, "
           "attributes_json = CAST(:attributes_json AS JSONB) WHERE short_code = :short_code;";
}

std::string PostgresSqlDialect::DeleteSql() const
{
    return "DELETE FROM links WHERE short_code = :short_code;";
}

std::string PostgresSqlDialect::ExistsSql() const
{
    return "SELECT COUNT(1) FROM links WHERE short_code = :short_code;";
}

std::string PostgresSqlDialect::ListSql() const
{
    return "SELECT id, short_code, target_url, "
           "CAST(EXTRACT(EPOCH FROM created_at) AS BIGINT) AS created_at, "
           "CAST(EXTRACT(EPOCH FROM updated_at) AS BIGINT) AS updated_at, "
           "CASE WHEN expires_at IS NULL THEN NULL ELSE CAST(EXTRACT(EPOCH FROM expires_at) AS BIGINT) END AS expires_at, "
           "CASE WHEN is_active THEN 1 ELSE 0 END AS is_active, "
           "owner_id, CASE WHEN attributes_json IS NULL THEN NULL ELSE attributes_json::text END AS attributes_json "
           "FROM links WHERE (:owner_id IS NULL OR owner_id = :owner_id) "
           "AND (:include_inactive = 1 OR is_active = TRUE) "
           "ORDER BY created_at LIMIT :limit OFFSET :offset;";
}

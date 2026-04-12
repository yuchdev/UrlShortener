#include "url_shortener/storage/sqlite/SqliteSessionFactory.hpp"

#include <soci/row.h>
#include <soci/session.h>
#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>

namespace {

class SqliteSession final : public ISqlSession {
public:
    SqliteSession(std::string dsn, std::chrono::milliseconds busy_timeout, SqliteErrorMapper mapper)
        : mapper_(mapper)
    {
        session_.open(soci::sqlite3, dsn);
        session_ << "PRAGMA busy_timeout = " + std::to_string(busy_timeout.count()) + ";";
    }

    bool Bootstrap(const std::string& sql, RepoError* error) override
    {
        try {
            session_ << sql;
            if (error) *error = RepoError::none;
            return true;
        } catch (const std::exception& ex) {
            if (error) *error = mapper_.MapException(ex);
            return false;
        }
    }

    bool InsertLink(const std::string& sql, const SqlRow& row, RepoError* error) override
    {
        try {
            session_ << sql,
                soci::use(*row.id, "id"),
                soci::use(*row.short_code, "short_code"),
                soci::use(*row.target_url, "target_url"),
                soci::use(*row.created_at, "created_at"),
                soci::use(*row.updated_at, "updated_at"),
                soci::use(row.expires_at, "expires_at"),
                soci::use(*row.is_active, "is_active"),
                soci::use(row.owner_id, "owner_id"),
                soci::use(row.attributes_json, "attributes_json");
            if (error) *error = RepoError::none;
            return true;
        } catch (const std::exception& ex) {
            if (error) *error = mapper_.MapException(ex);
            return false;
        }
    }

    std::optional<SqlRow> SelectByShortCode(const std::string& sql,
                                            const std::string& short_code,
                                            RepoError* error) override
    {
        try {
            soci::row r;
            soci::indicator ind = soci::i_ok;
            session_ << sql, soci::use(short_code, "short_code"), soci::into(r, ind);
            if (ind == soci::i_null) {
                if (error) *error = RepoError::not_found;
                return std::nullopt;
            }
            if (error) *error = RepoError::none;
            return RowFromSoci(r);
        } catch (const std::exception& ex) {
            if (error) *error = mapper_.MapException(ex);
            return std::nullopt;
        }
    }

    bool UpdateLink(const std::string& sql, const SqlRow& row, RepoError* error) override
    {
        try {
            session_ << sql,
                soci::use(*row.target_url, "target_url"),
                soci::use(*row.updated_at, "updated_at"),
                soci::use(row.expires_at, "expires_at"),
                soci::use(*row.is_active, "is_active"),
                soci::use(row.owner_id, "owner_id"),
                soci::use(row.attributes_json, "attributes_json"),
                soci::use(*row.short_code, "short_code");
            int affected = 0;
            session_ << "SELECT changes();", soci::into(affected);
            if (affected == 0) {
                if (error) *error = RepoError::not_found;
                return false;
            }
            if (error) *error = RepoError::none;
            return true;
        } catch (const std::exception& ex) {
            if (error) *error = mapper_.MapException(ex);
            return false;
        }
    }

    bool DeleteLink(const std::string& sql, const std::string& short_code, RepoError* error) override
    {
        try {
            session_ << sql, soci::use(short_code, "short_code");
            if (error) *error = RepoError::none;
            return true;
        } catch (const std::exception& ex) {
            if (error) *error = mapper_.MapException(ex);
            return false;
        }
    }

    bool Exists(const std::string& sql, const std::string& short_code, bool* exists, RepoError* error) override
    {
        try {
            int count = 0;
            session_ << sql, soci::use(short_code, "short_code"), soci::into(count);
            *exists = (count > 0);
            if (error) *error = RepoError::none;
            return true;
        } catch (const std::exception& ex) {
            if (error) *error = mapper_.MapException(ex);
            return false;
        }
    }

    std::vector<SqlRow> List(const std::string& sql,
                             const std::optional<std::string>& owner,
                             bool include_inactive,
                             std::size_t limit,
                             std::size_t offset,
                             RepoError* error) override
    {
        try {
            std::vector<SqlRow> rows;
            soci::rowset<soci::row> rs = (session_.prepare << sql,
                soci::use(owner, "owner_id"),
                soci::use(include_inactive ? 1 : 0, "include_inactive"),
                soci::use(static_cast<int>(limit), "limit"),
                soci::use(static_cast<int>(offset), "offset"));
            for (const auto& r : rs) {
                rows.push_back(RowFromSoci(r));
            }
            if (error) *error = RepoError::none;
            return rows;
        } catch (const std::exception& ex) {
            if (error) *error = mapper_.MapException(ex);
            return {};
        }
    }

private:
    static SqlRow RowFromSoci(const soci::row& r)
    {
        SqlRow row;
        row.id = r.get_indicator("id") == soci::i_null ? std::nullopt : std::optional<std::string>(r.get<std::string>("id"));
        row.short_code = r.get_indicator("short_code") == soci::i_null ? std::nullopt : std::optional<std::string>(r.get<std::string>("short_code"));
        row.target_url = r.get_indicator("target_url") == soci::i_null ? std::nullopt : std::optional<std::string>(r.get<std::string>("target_url"));
        row.created_at = r.get_indicator("created_at") == soci::i_null ? std::nullopt : std::optional<long long>(r.get<long long>("created_at"));
        row.updated_at = r.get_indicator("updated_at") == soci::i_null ? std::nullopt : std::optional<long long>(r.get<long long>("updated_at"));
        row.expires_at = r.get_indicator("expires_at") == soci::i_null ? std::nullopt : std::optional<long long>(r.get<long long>("expires_at"));
        row.is_active = r.get_indicator("is_active") == soci::i_null ? std::nullopt : std::optional<int>(r.get<int>("is_active"));
        row.owner_id = r.get_indicator("owner_id") == soci::i_null ? std::nullopt : std::optional<std::string>(r.get<std::string>("owner_id"));
        row.attributes_json = r.get_indicator("attributes_json") == soci::i_null ? std::nullopt : std::optional<std::string>(r.get<std::string>("attributes_json"));
        return row;
    }

    soci::session session_;
    SqliteErrorMapper mapper_;
};

} // namespace

SqliteSessionFactory::SqliteSessionFactory(SqlConnectionConfig config)
    : config_(std::move(config))
{
}

std::unique_ptr<ISqlSession> SqliteSessionFactory::Create(RepoError* error) const
{
    if (config_.backend != SqlBackendKind::sqlite || config_.dsn.empty()) {
        if (error) *error = RepoError::invalid_argument;
        return nullptr;
    }

    try {
        if (error) *error = RepoError::none;
        return std::make_unique<SqliteSession>(config_.dsn, config_.busy_timeout, error_mapper_);
    } catch (const std::exception& ex) {
        if (error) *error = error_mapper_.MapException(ex);
        return nullptr;
    }
}

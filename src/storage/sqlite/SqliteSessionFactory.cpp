#include "url_shortener/storage/sqlite/SqliteSessionFactory.hpp"

#include <soci/row.h>
#include <soci/session.h>
#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>

namespace {

// Helper to expand an optional<T> into a value + soci::indicator pair.
template <typename T>
static void ToSociIndicator(const std::optional<T>& opt, T& value, soci::indicator& ind)
{
    if (opt.has_value()) {
        value = *opt;
        ind = soci::i_ok;
    } else {
        value = T{};
        ind = soci::i_null;
    }
}

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
        std::string id        = row.id.value_or("");
        std::string short_code = row.short_code.value_or("");
        std::string target_url = row.target_url.value_or("");
        long long   created_at = row.created_at.value_or(0);
        long long   updated_at = row.updated_at.value_or(0);
        int         is_active  = row.is_active.value_or(0);

        long long   expires_at_val = 0;
        soci::indicator expires_at_ind = soci::i_null;
        ToSociIndicator(row.expires_at, expires_at_val, expires_at_ind);

        std::string owner_id_val;
        soci::indicator owner_id_ind = soci::i_null;
        ToSociIndicator(row.owner_id, owner_id_val, owner_id_ind);

        std::string attrs_json_val;
        soci::indicator attrs_json_ind = soci::i_null;
        ToSociIndicator(row.attributes_json, attrs_json_val, attrs_json_ind);

        try {
            session_ << sql,
                soci::use(id,              "id"),
                soci::use(short_code,      "short_code"),
                soci::use(target_url,      "target_url"),
                soci::use(created_at,      "created_at"),
                soci::use(updated_at,      "updated_at"),
                soci::use(expires_at_val,  expires_at_ind,  "expires_at"),
                soci::use(is_active,       "is_active"),
                soci::use(owner_id_val,    owner_id_ind,    "owner_id"),
                soci::use(attrs_json_val,  attrs_json_ind,  "attributes_json");
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
            soci::rowset<soci::row> rs = (session_.prepare << sql,
                soci::use(short_code, "short_code"));
            auto it = rs.begin();
            if (it == rs.end()) {
                if (error) *error = RepoError::not_found;
                return std::nullopt;
            }
            if (error) *error = RepoError::none;
            return RowFromSoci(*it);
        } catch (const std::exception& ex) {
            if (error) *error = mapper_.MapException(ex);
            return std::nullopt;
        }
    }

    bool UpdateLink(const std::string& sql, const SqlRow& row, RepoError* error) override
    {
        std::string target_url = row.target_url.value_or("");
        long long   updated_at = row.updated_at.value_or(0);
        int         is_active  = row.is_active.value_or(0);
        std::string short_code = row.short_code.value_or("");

        long long   expires_at_val = 0;
        soci::indicator expires_at_ind = soci::i_null;
        ToSociIndicator(row.expires_at, expires_at_val, expires_at_ind);

        std::string owner_id_val;
        soci::indicator owner_id_ind = soci::i_null;
        ToSociIndicator(row.owner_id, owner_id_val, owner_id_ind);

        std::string attrs_json_val;
        soci::indicator attrs_json_ind = soci::i_null;
        ToSociIndicator(row.attributes_json, attrs_json_val, attrs_json_ind);

        try {
            session_ << sql,
                soci::use(target_url,     "target_url"),
                soci::use(updated_at,     "updated_at"),
                soci::use(expires_at_val, expires_at_ind, "expires_at"),
                soci::use(is_active,      "is_active"),
                soci::use(owner_id_val,   owner_id_ind,   "owner_id"),
                soci::use(attrs_json_val, attrs_json_ind, "attributes_json"),
                soci::use(short_code,     "short_code");
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
        std::string owner_val;
        soci::indicator owner_ind = soci::i_null;
        ToSociIndicator(owner, owner_val, owner_ind);
        int include_inactive_int = include_inactive ? 1 : 0;
        int limit_int  = static_cast<int>(limit);
        int offset_int = static_cast<int>(offset);

        try {
            std::vector<SqlRow> rows;
            soci::rowset<soci::row> rs = (session_.prepare << sql,
                soci::use(owner_val,          owner_ind, "owner_id"),
                soci::use(include_inactive_int,           "include_inactive"),
                soci::use(limit_int,                      "limit"),
                soci::use(offset_int,                     "offset"));
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
    template <typename T>
    static std::optional<T> GetOpt(const soci::row& r, const std::string& name)
    {
        if (r.get_indicator(name) == soci::i_null) {
            return std::nullopt;
        }
        return r.get<T>(name);
    }

    // SQLite3 returns INTEGER columns as dt_integer (int) not dt_long_long.
    // Use this helper to safely retrieve integer columns as long long.
    static std::optional<long long> GetOptLL(const soci::row& r, const std::string& name)
    {
        if (r.get_indicator(name) == soci::i_null) {
            return std::nullopt;
        }
        const auto& props = r.get_properties(name);
        if (props.get_data_type() == soci::dt_long_long) {
            return r.get<long long>(name);
        }
        return static_cast<long long>(r.get<int>(name));
    }

    static SqlRow RowFromSoci(const soci::row& r)
    {
        SqlRow row;
        row.id             = GetOpt<std::string>(r, "id");
        row.short_code     = GetOpt<std::string>(r, "short_code");
        row.target_url     = GetOpt<std::string>(r, "target_url");
        row.created_at     = GetOptLL(r, "created_at");
        row.updated_at     = GetOptLL(r, "updated_at");
        row.expires_at     = GetOptLL(r, "expires_at");
        row.is_active      = GetOpt<int>(r, "is_active");
        row.owner_id       = GetOpt<std::string>(r, "owner_id");
        row.attributes_json = GetOpt<std::string>(r, "attributes_json");
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

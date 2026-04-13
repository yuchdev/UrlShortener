#include "url_shortener/storage/postgres/PostgresSessionFactory.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>

#include <soci/postgresql/soci-postgresql.h>
#include <soci/row.h>
#include <soci/session.h>
#include <soci/soci.h>

namespace {

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

static std::string BuildPostgresConnectString(const SqlConnectionConfig& config)
{
    std::string dsn = config.dsn;
    dsn += " connect_timeout=" + std::to_string(std::max(1LL, config.connect_timeout.count() / 1000));
    if (config.application_name.has_value()) {
        dsn += " application_name=" + *config.application_name;
    }
    return dsn;
}

class PostgresSession final : public ISqlSession {
public:
    PostgresSession(SqlConnectionConfig config, PostgresErrorMapper mapper)
        : config_(std::move(config)), mapper_(mapper)
    {
        session_.open(soci::postgresql, BuildPostgresConnectString(config_));
        session_ << "SET statement_timeout = " + std::to_string(config_.statement_timeout.count()) + ";";
    }

    bool Bootstrap(const std::string& sql, RepoError* error) override
    {
        return RunWithRetry([&]() { session_ << sql; }, true, error);
    }

    bool InsertLink(const std::string& sql, const SqlRow& row, RepoError* error) override
    {
        std::string id = row.id.value_or("");
        std::string short_code = row.short_code.value_or("");
        std::string target_url = row.target_url.value_or("");
        long long created_at = row.created_at.value_or(0);
        long long updated_at = row.updated_at.value_or(0);
        int is_active = row.is_active.value_or(0);

        long long expires_at_val = 0;
        soci::indicator expires_at_ind = soci::i_null;
        ToSociIndicator(row.expires_at, expires_at_val, expires_at_ind);

        std::string owner_id_val;
        soci::indicator owner_id_ind = soci::i_null;
        ToSociIndicator(row.owner_id, owner_id_val, owner_id_ind);

        std::string attrs_json_val;
        soci::indicator attrs_json_ind = soci::i_null;
        ToSociIndicator(row.attributes_json, attrs_json_val, attrs_json_ind);

        return RunWithRetry([&]() {
            session_ << sql,
                soci::use(id, "id"),
                soci::use(short_code, "short_code"),
                soci::use(target_url, "target_url"),
                soci::use(created_at, "created_at"),
                soci::use(updated_at, "updated_at"),
                soci::use(expires_at_val, expires_at_ind, "expires_at"),
                soci::use(is_active, "is_active"),
                soci::use(owner_id_val, owner_id_ind, "owner_id"),
                soci::use(attrs_json_val, attrs_json_ind, "attributes_json");
        }, false, error);
    }

    std::optional<SqlRow> SelectByShortCode(const std::string& sql,
                                            const std::string& short_code,
                                            RepoError* error) override
    {
        std::optional<SqlRow> out;
        bool ok = RunWithRetry([&]() {
            soci::rowset<soci::row> rs = (session_.prepare << sql, soci::use(short_code, "short_code"));
            auto it = rs.begin();
            if (it == rs.end()) {
                out = std::nullopt;
                return;
            }
            out = RowFromSoci(*it);
        }, true, error);
        if (!ok) {
            return std::nullopt;
        }
        if (!out && error) {
            *error = RepoError::not_found;
        }
        return out;
    }

    bool UpdateLink(const std::string& sql, const SqlRow& row, RepoError* error) override
    {
        std::string target_url = row.target_url.value_or("");
        long long updated_at = row.updated_at.value_or(0);
        int is_active = row.is_active.value_or(0);
        std::string short_code = row.short_code.value_or("");

        long long expires_at_val = 0;
        soci::indicator expires_at_ind = soci::i_null;
        ToSociIndicator(row.expires_at, expires_at_val, expires_at_ind);

        std::string owner_id_val;
        soci::indicator owner_id_ind = soci::i_null;
        ToSociIndicator(row.owner_id, owner_id_val, owner_id_ind);

        std::string attrs_json_val;
        soci::indicator attrs_json_ind = soci::i_null;
        ToSociIndicator(row.attributes_json, attrs_json_val, attrs_json_ind);

        return RunWithRetry([&]() {
            session_ << sql,
                soci::use(target_url, "target_url"),
                soci::use(updated_at, "updated_at"),
                soci::use(expires_at_val, expires_at_ind, "expires_at"),
                soci::use(is_active, "is_active"),
                soci::use(owner_id_val, owner_id_ind, "owner_id"),
                soci::use(attrs_json_val, attrs_json_ind, "attributes_json"),
                soci::use(short_code, "short_code");
            int affected = 0;
            session_ << "SELECT COUNT(1) FROM links WHERE short_code = :short_code;", soci::use(short_code, "short_code"), soci::into(affected);
            if (affected == 0) {
                throw std::runtime_error("not_found");
            }
        }, false, error);
    }

    bool DeleteLink(const std::string& sql, const std::string& short_code, RepoError* error) override
    {
        return RunWithRetry([&]() { session_ << sql, soci::use(short_code, "short_code"); }, false, error);
    }

    bool Exists(const std::string& sql, const std::string& short_code, bool* exists, RepoError* error) override
    {
        int count = 0;
        bool ok = RunWithRetry([&]() { session_ << sql, soci::use(short_code, "short_code"), soci::into(count); }, true, error);
        *exists = (count > 0);
        return ok;
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
        int limit_int = static_cast<int>(limit);
        int offset_int = static_cast<int>(offset);

        std::vector<SqlRow> rows;
        bool ok = RunWithRetry([&]() {
            soci::rowset<soci::row> rs = (session_.prepare << sql,
                                          soci::use(owner_val, owner_ind, "owner_id"),
                                          soci::use(include_inactive_int, "include_inactive"),
                                          soci::use(limit_int, "limit"),
                                          soci::use(offset_int, "offset"));
            for (const auto& r : rs) {
                rows.push_back(RowFromSoci(r));
            }
        }, true, error);
        if (!ok) {
            return {};
        }
        return rows;
    }

private:
    template <typename Fn>
    bool RunWithRetry(Fn&& fn, bool retryable, RepoError* error)
    {
        for (std::size_t attempt = 0; attempt <= config_.max_retries; ++attempt) {
            try {
                fn();
                if (error) *error = RepoError::none;
                return true;
            } catch (const std::exception& ex) {
                if (std::string(ex.what()) == "not_found") {
                    if (error) *error = RepoError::not_found;
                    return false;
                }
                const RepoError mapped = mapper_.MapException(ex);
                const bool transient = (mapped == RepoError::transient_failure || mapped == RepoError::timeout);
                if (!(retryable && transient && attempt < config_.max_retries)) {
                    if (error) *error = mapped;
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        if (error) *error = RepoError::transient_failure;
        return false;
    }

    template <typename T>
    static std::optional<T> GetOpt(const soci::row& r, const std::string& name)
    {
        if (r.get_indicator(name) == soci::i_null) {
            return std::nullopt;
        }
        return r.get<T>(name);
    }

    static SqlRow RowFromSoci(const soci::row& r)
    {
        SqlRow row;
        row.id = GetOpt<std::string>(r, "id");
        row.short_code = GetOpt<std::string>(r, "short_code");
        row.target_url = GetOpt<std::string>(r, "target_url");
        row.created_at = GetOpt<long long>(r, "created_at");
        row.updated_at = GetOpt<long long>(r, "updated_at");
        row.expires_at = GetOpt<long long>(r, "expires_at");
        row.is_active = GetOpt<int>(r, "is_active");
        row.owner_id = GetOpt<std::string>(r, "owner_id");
        row.attributes_json = GetOpt<std::string>(r, "attributes_json");
        return row;
    }

    SqlConnectionConfig config_;
    soci::session session_;
    PostgresErrorMapper mapper_;
};

} // namespace

PostgresSessionFactory::PostgresSessionFactory(SqlConnectionConfig config)
    : config_(std::move(config))
{
}

std::unique_ptr<ISqlSession> PostgresSessionFactory::Create(RepoError* error) const
{
    if (config_.backend != SqlBackendKind::postgres || config_.dsn.empty()) {
        if (error) *error = RepoError::invalid_argument;
        return nullptr;
    }

    try {
        if (error) *error = RepoError::none;
        return std::make_unique<PostgresSession>(config_, error_mapper_);
    } catch (const std::exception& ex) {
        if (error) *error = error_mapper_.MapException(ex);
        return nullptr;
    }
}

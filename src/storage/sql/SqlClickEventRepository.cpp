#include "url_shortener/storage/sql/SqlClickEventRepository.hpp"

#include <chrono>
#include <cstdint>

#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>
#include <soci/postgresql/soci-postgresql.h>

namespace url_shortener::storage::sql {

namespace {
long long to_epoch(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}
std::chrono::system_clock::time_point from_epoch_ll(long long s) {
    return std::chrono::system_clock::time_point{std::chrono::seconds(s)};
}
bool is_postgres_dsn(const std::string& dsn) {
    return dsn.rfind("postgresql://", 0) == 0
        || dsn.rfind("postgres://", 0) == 0
        || dsn.find("host=") != std::string::npos
        || dsn.find("dbname=") != std::string::npos;
}
} // namespace

SqlClickEventRepository::SqlClickEventRepository(const std::string& dsn) {
    is_postgres_ = is_postgres_dsn(dsn);
    if (is_postgres_) {
        session_.open(soci::postgresql, dsn);
    } else {
        session_.open(soci::sqlite3, dsn);
    }
    Bootstrap(is_postgres_);
}

void SqlClickEventRepository::Bootstrap(bool is_postgres) {
    if (is_postgres) {
        session_ <<
            "CREATE TABLE IF NOT EXISTS click_events ("
            "event_id TEXT PRIMARY KEY,"
            "occurred_at TIMESTAMPTZ NOT NULL,"
            "slug TEXT NOT NULL,"
            "link_id TEXT NOT NULL,"
            "domain TEXT NOT NULL,"
            "status_code INTEGER NOT NULL,"
            "referrer TEXT,"
            "user_agent TEXT,"
            "client_id_hash TEXT"
            ");";
    } else {
        session_ <<
            "CREATE TABLE IF NOT EXISTS click_events ("
            "event_id TEXT PRIMARY KEY,"
            "occurred_at INTEGER NOT NULL,"
            "slug TEXT NOT NULL,"
            "link_id TEXT NOT NULL,"
            "domain TEXT NOT NULL,"
            "status_code INTEGER NOT NULL,"
            "referrer TEXT,"
            "user_agent TEXT,"
            "client_id_hash TEXT"
            ");";
    }
    session_ << "CREATE INDEX IF NOT EXISTS idx_ce_slug_ts "
                "ON click_events(slug, occurred_at);";
    session_ << "CREATE INDEX IF NOT EXISTS idx_ce_slug_sc "
                "ON click_events(slug, status_code);";
    session_ << "CREATE INDEX IF NOT EXISTS idx_ce_slug_domain "
                "ON click_events(slug, domain);";
}

bool SqlClickEventRepository::InsertBatch(
    const std::vector<analytics::ClickEvent>& events,
    analytics::AnalyticsError* error)
{
    std::scoped_lock lk(mu_);
    try {
        for (const auto& e : events) {
            int sc = static_cast<int>(e.status_code);
            long long oat = to_epoch(e.occurred_at);
            if (is_postgres_) {
                session_ <<
                    "INSERT INTO click_events "
                    "(event_id, occurred_at, slug, link_id, domain, status_code, "
                    " referrer, user_agent, client_id_hash) "
                    "VALUES (:eid, to_timestamp(:oat), :slug, :lid, :dom, :sc, :ref, :ua, :cih) "
                    "ON CONFLICT (event_id) DO NOTHING",
                    soci::use(e.event_id,       "eid"),
                    soci::use(oat,              "oat"),
                    soci::use(e.slug,           "slug"),
                    soci::use(e.link_id,        "lid"),
                    soci::use(e.domain,         "dom"),
                    soci::use(sc,               "sc"),
                    soci::use(e.referrer,       "ref"),
                    soci::use(e.user_agent,     "ua"),
                    soci::use(e.client_id_hash, "cih");
            } else {
                session_ <<
                    "INSERT OR IGNORE INTO click_events "
                    "(event_id, occurred_at, slug, link_id, domain, status_code, "
                    " referrer, user_agent, client_id_hash) "
                    "VALUES (:eid, :oat, :slug, :lid, :dom, :sc, :ref, :ua, :cih)",
                    soci::use(e.event_id,       "eid"),
                    soci::use(oat,              "oat"),
                    soci::use(e.slug,           "slug"),
                    soci::use(e.link_id,        "lid"),
                    soci::use(e.domain,         "dom"),
                    soci::use(sc,               "sc"),
                    soci::use(e.referrer,       "ref"),
                    soci::use(e.user_agent,     "ua"),
                    soci::use(e.client_id_hash, "cih");
            }
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) { error->code = analytics::AnalyticsErrorCode::repository; error->message = ex.what(); }
        return false;
    }
}

bool SqlClickEventRepository::GetAggregateStats(
    const analytics::AggregateQuery& query,
    analytics::AggregateStats* stats,
    analytics::AnalyticsError* error)
{
    if (!stats) return false;
    std::scoped_lock lk(mu_);
    *stats = {};
    try {
        long long from_e = to_epoch(query.from);
        long long to_e   = to_epoch(query.to);

        // Build WHERE clause; use integer epoch for SQLite, to_timestamp() for PostgreSQL.
        const std::string ts_cond = is_postgres_
            ? "occurred_at >= to_timestamp(:from_ts) AND occurred_at < to_timestamp(:to_ts)"
            : "occurred_at >= :from_ts AND occurred_at < :to_ts";
        const std::string slug_clause = query.slug ? " AND slug = :slug" : "";
        const std::string slug_val    = query.slug ? *query.slug : "";
        const std::string base_where  = "WHERE " + ts_cond + slug_clause;

        // Helper: execute a COUNT(*) query
        auto count_query = [&](const std::string& extra_where) -> std::int64_t {
            std::string cnt_text = "0";
            const std::string sql = "SELECT CAST(COUNT(*) AS TEXT) FROM click_events " + base_where + extra_where;
            if (query.slug) {
                session_ << sql,
                    soci::use(from_e, "from_ts"), soci::use(to_e, "to_ts"),
                    soci::use(slug_val, "slug"), soci::into(cnt_text);
            } else {
                session_ << sql,
                    soci::use(from_e, "from_ts"), soci::use(to_e, "to_ts"),
                    soci::into(cnt_text);
            }
            return static_cast<std::int64_t>(std::stoll(cnt_text));
        };

        stats->total_attempts     = static_cast<uint64_t>(count_query(""));
        stats->successful_redirects = static_cast<uint64_t>(
            count_query(" AND status_code >= 300 AND status_code < 400"));

        // Counts by status code
        {
            const std::string sql =
                "SELECT CAST(status_code AS TEXT) AS status_code, "
                "CAST(COUNT(*) AS TEXT) AS cnt FROM click_events "
                + base_where + " GROUP BY status_code";
            soci::rowset<soci::row> rs = query.slug
                ? (session_.prepare << sql,
                   soci::use(from_e, "from_ts"), soci::use(to_e, "to_ts"),
                   soci::use(slug_val, "slug"))
                : (session_.prepare << sql,
                   soci::use(from_e, "from_ts"), soci::use(to_e, "to_ts"));
            for (const auto& r : rs) {
                analytics::StatusCodeCount sc;
                sc.status_code = static_cast<uint16_t>(std::stoll(r.get<std::string>("status_code")));
                sc.count       = static_cast<uint64_t>(std::stoll(r.get<std::string>("cnt")));
                stats->status_code_counts.push_back(sc);
            }
        }

        // Counts by domain
        {
            const std::string sql =
                "SELECT domain, CAST(COUNT(*) AS TEXT) AS cnt FROM click_events "
                + base_where + " GROUP BY domain";
            soci::rowset<soci::row> rs = query.slug
                ? (session_.prepare << sql,
                   soci::use(from_e, "from_ts"), soci::use(to_e, "to_ts"),
                   soci::use(slug_val, "slug"))
                : (session_.prepare << sql,
                   soci::use(from_e, "from_ts"), soci::use(to_e, "to_ts"));
            for (const auto& r : rs) {
                analytics::DomainCount dc;
                dc.domain = r.get<std::string>("domain");
                dc.count  = static_cast<uint64_t>(std::stoll(r.get<std::string>("cnt")));
                stats->domain_counts.push_back(dc);
            }
        }

        // Time buckets (SQLite only; PostgreSQL would need date_trunc)
        if (query.bucket && !is_postgres_) {
            long long width = *query.bucket == analytics::AggregateBucket::hour
                ? 3600LL
                : (*query.bucket == analytics::AggregateBucket::day ? 86400LL : 604800LL);
            const std::string sql =
                "SELECT CAST((occurred_at / :w) * :w AS TEXT) AS bucket_start, "
                "CAST(COUNT(*) AS TEXT) AS cnt "
                "FROM click_events " + base_where
                + " GROUP BY bucket_start ORDER BY bucket_start";
            soci::rowset<soci::row> rs = query.slug
                ? (session_.prepare << sql,
                   soci::use(width, "w"),
                   soci::use(from_e, "from_ts"), soci::use(to_e, "to_ts"),
                   soci::use(slug_val, "slug"))
                : (session_.prepare << sql,
                   soci::use(width, "w"),
                   soci::use(from_e, "from_ts"), soci::use(to_e, "to_ts"));
            for (const auto& r : rs) {
                analytics::TimeBucketCount tb;
                std::int64_t bs = static_cast<std::int64_t>(std::stoll(r.get<std::string>("bucket_start")));
                tb.bucket_start = from_epoch_ll(bs);
                tb.count        = static_cast<uint64_t>(std::stoll(r.get<std::string>("cnt")));
                stats->time_buckets.push_back(tb);
            }
        }

        return true;
    } catch (const std::exception& ex) {
        if (error) { error->code = analytics::AnalyticsErrorCode::repository; error->message = ex.what(); }
        return false;
    }
}

bool SqlClickEventRepository::DeleteOlderThan(
    analytics::Timestamp cutoff,
    analytics::AnalyticsError* error)
{
    std::scoped_lock lk(mu_);
    try {
        long long cutoff_epoch = to_epoch(cutoff);
        if (is_postgres_) {
            session_ << "DELETE FROM click_events WHERE occurred_at < to_timestamp(:cutoff)",
                soci::use(cutoff_epoch, "cutoff");
        } else {
            session_ << "DELETE FROM click_events WHERE occurred_at < :cutoff",
                soci::use(cutoff_epoch, "cutoff");
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) { error->code = analytics::AnalyticsErrorCode::repository; error->message = ex.what(); }
        return false;
    }
}

}

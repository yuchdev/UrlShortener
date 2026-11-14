#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "url_shortener/storage/sql/sql_click_event_repository.hpp"

#include <soci/postgresql/soci-postgresql.h>
#include <soci/soci.h>
#include <soci/sqlite3/soci-sqlite3.h>

namespace url_shortener::storage::sql
{

namespace
{
long long to_epoch(const std::chrono::system_clock::time_point& tp)
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               tp.time_since_epoch())
        .count();
}
std::chrono::system_clock::time_point from_epoch_ll(long long s)
{
    return std::chrono::system_clock::time_point {std::chrono::seconds(s)};
}
bool is_postgres_dsn(const std::string& dsn)
{
    return dsn.rfind("postgresql://", 0) == 0
        || dsn.rfind("postgres://", 0) == 0
        || dsn.find("host=") != std::string::npos
        || dsn.find("dbname=") != std::string::npos;
}
/// Builds the ORDER BY clause for `ListEvents`. The column name is chosen from
/// a fixed set of string literals by a `switch` over `ClickEventSort` - it
/// never derives from user text - and the final tie-break `event_id ASC` keeps
/// the ordering stable and deterministic across backends.
/// @param sort Primary sort column selected by the caller.
/// @param descending `true` orders the primary column DESC, `false` ASC; the
/// `event_id` tie-break is always ASC regardless.
/// @return An ORDER BY body such as `occurred_at DESC, event_id ASC`.
std::string list_order_clause(analytics::ClickEventSort sort, bool descending)
{
    const char* col = "occurred_at";
    switch (sort) {
        case analytics::ClickEventSort::occurred_at:
            col = "occurred_at";
            break;
        case analytics::ClickEventSort::slug:
            col = "slug";
            break;
        case analytics::ClickEventSort::status_code:
            col = "status_code";
            break;
    }
    const char* dir = descending ? "DESC" : "ASC";
    return std::string(col) + " " + dir + ", event_id ASC";
}

/// Escapes a `text` filter for a `LIKE ... ESCAPE '\\'` clause and lower-cases
/// ASCII so matching is case-insensitive identically on SQLite and PostgreSQL.
/// `\\`, `%`, and `_` are prefixed with `\\` so they match literally rather
/// than as wildcards. The result is wrapped in `%...%` by the caller and bound
/// as a parameter - it is never concatenated into SQL text.
/// @param text Raw, untrusted filter text.
/// @return The escaped, lower-cased pattern body (without the surrounding `%`).
std::string escape_like(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text) {
        char lc =
            static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lc == '\\' || lc == '%' || lc == '_') {
            out.push_back('\\');
        }
        out.push_back(lc);
    }
    return out;
}
}  // namespace

SqlClickEventRepository::SqlClickEventRepository(const std::string& dsn)
{
    is_postgres_ = is_postgres_dsn(dsn);
    if (is_postgres_) {
        session_.open(soci::postgresql, dsn);
    }
    else {
        session_.open(soci::sqlite3, dsn);
    }
    Bootstrap(is_postgres_);
}

void SqlClickEventRepository::Bootstrap(bool is_postgres)
{
    if (is_postgres) {
        session_ << "CREATE TABLE IF NOT EXISTS click_events ("
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
    }
    else {
        session_ << "CREATE TABLE IF NOT EXISTS click_events ("
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
                session_ << "INSERT INTO click_events "
                            "(event_id, occurred_at, slug, link_id, domain, "
                            "status_code, "
                            " referrer, user_agent, client_id_hash) "
                            "VALUES (:eid, to_timestamp(:oat), :slug, :lid, "
                            ":dom, :sc, :ref, :ua, :cih) "
                            "ON CONFLICT (event_id) DO NOTHING",
                    soci::use(e.event_id, "eid"), soci::use(oat, "oat"),
                    soci::use(e.slug, "slug"), soci::use(e.link_id, "lid"),
                    soci::use(e.domain, "dom"), soci::use(sc, "sc"),
                    soci::use(e.referrer, "ref"), soci::use(e.user_agent, "ua"),
                    soci::use(e.client_id_hash, "cih");
            }
            else {
                session_ << "INSERT OR IGNORE INTO click_events "
                            "(event_id, occurred_at, slug, link_id, domain, "
                            "status_code, "
                            " referrer, user_agent, client_id_hash) "
                            "VALUES (:eid, :oat, :slug, :lid, :dom, :sc, :ref, "
                            ":ua, :cih)",
                    soci::use(e.event_id, "eid"), soci::use(oat, "oat"),
                    soci::use(e.slug, "slug"), soci::use(e.link_id, "lid"),
                    soci::use(e.domain, "dom"), soci::use(sc, "sc"),
                    soci::use(e.referrer, "ref"), soci::use(e.user_agent, "ua"),
                    soci::use(e.client_id_hash, "cih");
            }
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            error->code = analytics::AnalyticsErrorCode::repository;
            error->message = ex.what();
        }
        return false;
    }
}

bool SqlClickEventRepository::GetAggregateStats(
    const analytics::AggregateQuery& query,
    analytics::AggregateStats* stats,
    analytics::AnalyticsError* error)
{
    if (!stats)
        return false;
    std::scoped_lock lk(mu_);
    *stats = {};
    try {
        long long from_e = to_epoch(query.from);
        long long to_e = to_epoch(query.to);

        // Build WHERE clause; use integer epoch for SQLite, to_timestamp() for
        // PostgreSQL.
        const std::string ts_cond = is_postgres_
            ? "occurred_at >= to_timestamp(:from_ts) AND occurred_at < "
              "to_timestamp(:to_ts)"
            : "occurred_at >= :from_ts AND occurred_at < :to_ts";
        const std::string slug_clause = query.slug ? " AND slug = :slug" : "";
        const std::string slug_val = query.slug ? *query.slug : "";
        const std::string base_where = "WHERE " + ts_cond + slug_clause;

        // Helper: execute a COUNT(*) query
        auto count_query = [&](const std::string& extra_where) -> std::int64_t
        {
            std::string cnt_text = "0";
            const std::string sql =
                "SELECT CAST(COUNT(*) AS TEXT) FROM click_events " + base_where
                + extra_where;
            if (query.slug) {
                session_ << sql, soci::use(from_e, "from_ts"),
                    soci::use(to_e, "to_ts"), soci::use(slug_val, "slug"),
                    soci::into(cnt_text);
            }
            else {
                session_ << sql, soci::use(from_e, "from_ts"),
                    soci::use(to_e, "to_ts"), soci::into(cnt_text);
            }
            return static_cast<std::int64_t>(std::stoll(cnt_text));
        };

        stats->total_attempts = static_cast<uint64_t>(count_query(""));
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
                   soci::use(from_e, "from_ts"),
                   soci::use(to_e, "to_ts"),
                   soci::use(slug_val, "slug"))
                : (session_.prepare << sql,
                   soci::use(from_e, "from_ts"),
                   soci::use(to_e, "to_ts"));
            for (const auto& r : rs) {
                analytics::StatusCodeCount sc;
                sc.status_code = static_cast<uint16_t>(
                    std::stoll(r.get<std::string>("status_code")));
                sc.count = static_cast<uint64_t>(
                    std::stoll(r.get<std::string>("cnt")));
                stats->status_code_counts.push_back(sc);
            }
        }

        // Counts by domain
        {
            const std::string sql =
                "SELECT domain, CAST(COUNT(*) AS TEXT) AS cnt FROM "
                "click_events "
                + base_where + " GROUP BY domain";
            soci::rowset<soci::row> rs = query.slug
                ? (session_.prepare << sql,
                   soci::use(from_e, "from_ts"),
                   soci::use(to_e, "to_ts"),
                   soci::use(slug_val, "slug"))
                : (session_.prepare << sql,
                   soci::use(from_e, "from_ts"),
                   soci::use(to_e, "to_ts"));
            for (const auto& r : rs) {
                analytics::DomainCount dc;
                dc.domain = r.get<std::string>("domain");
                dc.count = static_cast<uint64_t>(
                    std::stoll(r.get<std::string>("cnt")));
                stats->domain_counts.push_back(dc);
            }
        }

        // Time buckets (SQLite only; PostgreSQL would need date_trunc)
        if (query.bucket && !is_postgres_) {
            long long width = *query.bucket == analytics::AggregateBucket::hour
                ? 3600LL
                : (*query.bucket == analytics::AggregateBucket::day ? 86400LL
                                                                    : 604800LL);
            const std::string sql =
                "SELECT CAST((occurred_at / :w) * :w AS TEXT) AS bucket_start, "
                "CAST(COUNT(*) AS TEXT) AS cnt "
                "FROM click_events "
                + base_where + " GROUP BY bucket_start ORDER BY bucket_start";
            soci::rowset<soci::row> rs = query.slug
                ? (session_.prepare << sql,
                   soci::use(width, "w"),
                   soci::use(from_e, "from_ts"),
                   soci::use(to_e, "to_ts"),
                   soci::use(slug_val, "slug"))
                : (session_.prepare << sql,
                   soci::use(width, "w"),
                   soci::use(from_e, "from_ts"),
                   soci::use(to_e, "to_ts"));
            for (const auto& r : rs) {
                analytics::TimeBucketCount tb;
                std::int64_t bs = static_cast<std::int64_t>(
                    std::stoll(r.get<std::string>("bucket_start")));
                tb.bucket_start = from_epoch_ll(bs);
                tb.count = static_cast<uint64_t>(
                    std::stoll(r.get<std::string>("cnt")));
                stats->time_buckets.push_back(tb);
            }
        }

        return true;
    } catch (const std::exception& ex) {
        if (error) {
            error->code = analytics::AnalyticsErrorCode::repository;
            error->message = ex.what();
        }
        return false;
    }
}

bool SqlClickEventRepository::DeleteOlderThan(analytics::Timestamp cutoff,
                                              analytics::AnalyticsError* error)
{
    std::scoped_lock lk(mu_);
    try {
        long long cutoff_epoch = to_epoch(cutoff);
        if (is_postgres_) {
            session_ << "DELETE FROM click_events WHERE occurred_at < "
                        "to_timestamp(:cutoff)",
                soci::use(cutoff_epoch, "cutoff");
        }
        else {
            session_ << "DELETE FROM click_events WHERE occurred_at < :cutoff",
                soci::use(cutoff_epoch, "cutoff");
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            error->code = analytics::AnalyticsErrorCode::repository;
            error->message = ex.what();
        }
        return false;
    }
}

bool SqlClickEventRepository::ListEvents(
    const analytics::ClickEventQuery& query,
    analytics::ClickEventPage* page,
    analytics::AnalyticsError* error)
{
    if (!page) {
        if (error) {
            error->code = analytics::AnalyticsErrorCode::validation;
            error->message = "ListEvents requires a non-null page";
        }
        return false;
    }
    if (query.from && query.to && *query.from > *query.to) {
        if (error) {
            error->code = analytics::AnalyticsErrorCode::validation;
            error->message = "ListEvents range is inverted (from > to)";
        }
        return false;
    }
    if (query.text
        && query.text->size() > analytics::kMaxClickEventTextFilterLength)
    {
        if (error) {
            error->code = analytics::AnalyticsErrorCode::validation;
            error->message = "ListEvents text filter exceeds maximum length";
        }
        return false;
    }

    std::scoped_lock lk(mu_);
    try {
        // Every value derived from the query is bound; absent filters use a
        // neutral value plus has_* = 0 so one SQL text per backend serves all
        // query shapes.
        int has_slug = query.slug ? 1 : 0;
        std::string slug_val = query.slug ? *query.slug : std::string();
        int has_status = query.status_code ? 1 : 0;
        int sc_val =
            query.status_code ? static_cast<int>(*query.status_code) : 0;
        int has_text = query.text ? 1 : 0;
        std::string pat =
            query.text ? ("%" + escape_like(*query.text) + "%") : std::string();
        int has_from = query.from ? 1 : 0;
        long long from_ts = query.from ? to_epoch(*query.from) : 0;
        int has_to = query.to ? 1 : 0;
        long long to_ts = query.to ? to_epoch(*query.to) : 0;

        // <ts(x)> is the bound epoch value on SQLite, to_timestamp(x) on
        // Postgres.
        const std::string from_expr =
            is_postgres_ ? "to_timestamp(:from_ts)" : ":from_ts";
        const std::string to_expr =
            is_postgres_ ? "to_timestamp(:to_ts)" : ":to_ts";
        const std::string where =
            " WHERE (:has_slug = 0 OR slug = :slug)"
            " AND (:has_status = 0 OR status_code = :sc)"
            " AND (:has_text = 0 OR (lower(slug) LIKE :pat ESCAPE '\\'"
            " OR lower(domain) LIKE :pat ESCAPE '\\'))"
            " AND (:has_from = 0 OR occurred_at >= " + from_expr + ")"
            " AND (:has_to = 0 OR occurred_at < " + to_expr + ")";

        analytics::ClickEventPage result;

        // 1) Total matches, before offset/limit.
        std::string count_text = "0";
        const std::string count_sql =
            "SELECT CAST(COUNT(*) AS TEXT) FROM click_events" + where;
        session_ << count_sql, soci::use(has_slug, "has_slug"),
            soci::use(slug_val, "slug"), soci::use(has_status, "has_status"),
            soci::use(sc_val, "sc"), soci::use(has_text, "has_text"),
            soci::use(pat, "pat"), soci::use(has_from, "has_from"),
            soci::use(from_ts, "from_ts"), soci::use(has_to, "has_to"),
            soci::use(to_ts, "to_ts"), soci::into(count_text);
        result.total = static_cast<std::uint64_t>(std::stoll(count_text));

        // 2) Ordered, clamped window. occurred_at and status_code are read as
        // TEXT to dodge SOCI integer-type differences across backends.
        long long lim = static_cast<long long>(
            std::min(query.limit, analytics::kMaxClickEventPageSize));
        // Clamp the offset to LLONG_MAX before narrowing to the bound SQL
        // integer: a size_t offset above LLONG_MAX would otherwise wrap
        // negative (PostgreSQL rejects it, SQLite silently treats it as 0).
        // Clamping yields a well-defined empty window while `total` is still
        // counted across every match, matching the in-memory adapter's "offset
        // past the end -> empty page" behavior.
        constexpr std::size_t kMaxOffset =
            static_cast<std::size_t>(std::numeric_limits<long long>::max());
        long long off =
            static_cast<long long>(std::min(query.offset, kMaxOffset));
        const std::string occurred_expr = is_postgres_
            ? "CAST(CAST(EXTRACT(EPOCH FROM occurred_at) AS BIGINT) AS TEXT)"
            : "CAST(occurred_at AS TEXT)";
        const std::string row_sql =
            "SELECT event_id, " + occurred_expr + " AS occurred_s, slug,"
            " link_id, domain, CAST(status_code AS TEXT) AS status_s,"
            " referrer, user_agent, client_id_hash FROM click_events" + where
            + " ORDER BY " + list_order_clause(query.sort_by, query.descending)
            + " LIMIT :limit OFFSET :offset";
        soci::rowset<soci::row> rs = (session_.prepare << row_sql,
                                      soci::use(has_slug, "has_slug"),
                                      soci::use(slug_val, "slug"),
                                      soci::use(has_status, "has_status"),
                                      soci::use(sc_val, "sc"),
                                      soci::use(has_text, "has_text"),
                                      soci::use(pat, "pat"),
                                      soci::use(has_from, "has_from"),
                                      soci::use(from_ts, "from_ts"),
                                      soci::use(has_to, "has_to"),
                                      soci::use(to_ts, "to_ts"),
                                      soci::use(lim, "limit"),
                                      soci::use(off, "offset"));
        for (const auto& r : rs) {
            analytics::ClickEvent e;
            e.event_id = r.get<std::string>("event_id");
            e.occurred_at =
                from_epoch_ll(std::stoll(r.get<std::string>("occurred_s")));
            e.slug = r.get<std::string>("slug");
            e.link_id = r.get<std::string>("link_id");
            e.domain = r.get<std::string>("domain");
            e.status_code = static_cast<std::uint16_t>(
                std::stoll(r.get<std::string>("status_s")));
            e.referrer = r.get_indicator("referrer") == soci::i_null
                ? std::string()
                : r.get<std::string>("referrer");
            e.user_agent = r.get_indicator("user_agent") == soci::i_null
                ? std::string()
                : r.get<std::string>("user_agent");
            e.client_id_hash = r.get_indicator("client_id_hash") == soci::i_null
                ? std::string()
                : r.get<std::string>("client_id_hash");
            result.items.push_back(std::move(e));
        }

        // Publish atomically so `page` is untouched on any failure above.
        *page = std::move(result);
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            error->code = analytics::AnalyticsErrorCode::repository;
            error->message = ex.what();
        }
        return false;
    }
}

}  // namespace url_shortener::storage::sql

#pragma once

#include <string>
#include <mutex>

#include <soci/session.h>

#include "url_shortener/analytics/IClickEventRepository.hpp"

namespace url_shortener::storage::sql {

/**
 * @brief SQL-backed click-event repository (SQLite or PostgreSQL via SOCI).
 *
 * Pass a file path or ":memory:" for SQLite, or a PostgreSQL connection string
 * (e.g. "host=localhost dbname=mydb user=u ******") for PostgreSQL.
 * The schema is bootstrapped automatically on first use.
 */
class SqlClickEventRepository final : public analytics::IClickEventRepository {
public:
    explicit SqlClickEventRepository(const std::string& dsn = ":memory:");

    bool InsertBatch(const std::vector<analytics::ClickEvent>& events, analytics::AnalyticsError* error = nullptr) override;
    bool GetAggregateStats(const analytics::AggregateQuery& query, analytics::AggregateStats* stats, analytics::AnalyticsError* error = nullptr) override;
    bool DeleteOlderThan(analytics::Timestamp cutoff, analytics::AnalyticsError* error = nullptr) override;

private:
    void Bootstrap(bool is_postgres);

    soci::session session_;
    bool is_postgres_ = false;
    mutable std::mutex mu_;
};

}

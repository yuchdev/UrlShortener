#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/analytics/click_event_query.hpp"
#include "url_shortener/storage/sql/sql_click_event_repository.hpp"

/**
 * @file
 * @brief Backend-neutral `SqlClickEventRepository::ListEvents` test cases,
 * shared by the SQLite and PostgreSQL integration suites so both backends are
 * asserted to behave identically. Each case clears the repository first (via
 * `DeleteOlderThan` with a far-future cutoff) so it is isolated on a reused
 * SQLite file and on the shared Postgres database alike.
 */
namespace ce_list_test
{

using url_shortener::analytics::AnalyticsError;
using url_shortener::analytics::AnalyticsErrorCode;
using url_shortener::analytics::ClickEvent;
using url_shortener::analytics::ClickEventPage;
using url_shortener::analytics::ClickEventQuery;
using url_shortener::analytics::ClickEventSort;
using url_shortener::analytics::kMaxClickEventPageSize;
using url_shortener::analytics::Timestamp;
using Repo = url_shortener::storage::sql::SqlClickEventRepository;

/// Builds a timestamp @p secs seconds past the epoch for deterministic
/// ordering/boundary assertions.
inline Timestamp ts(long long secs)
{
    return Timestamp {std::chrono::seconds(secs)};
}

/// Builds a fully-populated click event; `client_id_hash`/`link_id` derive from
/// @p id so round-tripping can be asserted.
inline ClickEvent ev(std::string id,
                     std::string slug,
                     std::string domain,
                     std::uint16_t status,
                     long long secs,
                     std::string referrer = "r",
                     std::string user_agent = "ua")
{
    ClickEvent e;
    e.event_id = std::move(id);
    e.slug = std::move(slug);
    e.domain = std::move(domain);
    e.status_code = status;
    e.occurred_at = ts(secs);
    e.link_id = "L-" + e.event_id;
    e.referrer = std::move(referrer);
    e.user_agent = std::move(user_agent);
    e.client_id_hash = "hash-" + e.event_id;
    return e;
}

/// Comma-joins the `event_id`s of a page in result order for compact
/// order/window assertions.
inline std::string ids(const ClickEventPage& page)
{
    std::string out;
    for (const auto& e : page.items) {
        if (!out.empty()) {
            out += ",";
        }
        out += e.event_id;
    }
    return out;
}

/// Deletes every seeded event (all at small epochs) so each case starts from an
/// empty table on both backends.
inline void clear(Repo& repo)
{
    BOOST_TEST(repo.DeleteOlderThan(ts(4102444800LL), nullptr));
}

/// Seeds the deterministic 12-event fixture: slugs alpha/beta/gamma (x4 each),
/// domains one/two.example (x6 each), status 301/302/404 (x4 each),
/// `occurred_at` 100..1200 ascending with `event_id`.
inline void seed12(Repo& repo)
{
    const std::vector<ClickEvent> batch = {
        ev("e01", "alpha", "one.example", 301, 100),
        ev("e02", "alpha", "two.example", 302, 200),
        ev("e03", "alpha", "one.example", 404, 300),
        ev("e04", "alpha", "two.example", 301, 400),
        ev("e05", "beta", "one.example", 302, 500),
        ev("e06", "beta", "two.example", 404, 600),
        ev("e07", "beta", "one.example", 301, 700),
        ev("e08", "beta", "two.example", 302, 800),
        ev("e09", "gamma", "one.example", 404, 900),
        ev("e10", "gamma", "two.example", 301, 1000),
        ev("e11", "gamma", "one.example", 302, 1100),
        ev("e12", "gamma", "two.example", 404, 1200),
    };
    BOOST_TEST(repo.InsertBatch(batch, nullptr));
}

/// Each filter alone and combined; `text` matches slug OR domain
/// case-insensitively.
inline void case_filters(Repo& repo)
{
    clear(repo);
    seed12(repo);
    {
        ClickEventQuery q;
        q.slug = "beta";
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 4u);
        BOOST_TEST(p.items.size() == 4u);
    }
    {
        ClickEventQuery q;
        q.status_code = static_cast<std::uint16_t>(404);
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 4u);
    }
    {
        // text over domain, upper-cased input -> case-insensitive match.
        ClickEventQuery q;
        q.text = "TWO.";
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 6u);
    }
    {
        // text over slug, case-insensitive.
        ClickEventQuery q;
        q.text = "ALPHA";
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 4u);
    }
    {
        // Combined slug + status, with a deterministic order assertion.
        ClickEventQuery q;
        q.slug = "alpha";
        q.status_code = static_cast<std::uint16_t>(301);
        q.sort_by = ClickEventSort::occurred_at;
        q.descending = false;
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 2u);
        BOOST_TEST(ids(p) == "e01,e04");
    }
}

/// Half-open time window: `from` inclusive, `to` exclusive.
inline void case_time_window(Repo& repo)
{
    clear(repo);
    seed12(repo);
    ClickEventQuery q;
    q.from = ts(300);  // inclusive -> includes e03
    q.to = ts(600);  // exclusive -> excludes e06
    q.sort_by = ClickEventSort::occurred_at;
    q.descending = false;
    q.limit = 100;
    ClickEventPage p;
    BOOST_TEST(repo.ListEvents(q, &p, nullptr));
    BOOST_TEST(p.total == 3u);
    BOOST_TEST(ids(p) == "e03,e04,e05");
}

/// `%` and `_` in `text` match literally (ESCAPE), not as LIKE wildcards.
inline void case_text_wildcard_literal(Repo& repo)
{
    clear(repo);
    const std::vector<ClickEvent> batch = {
        ev("w1", "a%b", "d.example", 302, 10),
        ev("w2", "a_b", "d.example", 302, 20),
        ev("w3", "axb", "d.example", 302, 30),
        ev("w4", "a100b", "d.example", 302, 40),
    };
    BOOST_TEST(repo.InsertBatch(batch, nullptr));
    {
        // Unescaped, "a%b" would match all four; escaped, only the literal.
        ClickEventQuery q;
        q.text = "a%b";
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 1u);
        BOOST_TEST(ids(p) == "w1");
    }
    {
        // Unescaped, "a_b" would match a%b/a_b/axb; escaped, only the literal.
        ClickEventQuery q;
        q.text = "a_b";
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 1u);
        BOOST_TEST(ids(p) == "w2");
    }
}

/// Every sort field in both directions, with `event_id` ascending tie-break.
inline void case_sort_all(Repo& repo)
{
    clear(repo);
    seed12(repo);
    const auto run = [&](ClickEventSort sort, bool descending)
    {
        ClickEventQuery q;
        q.sort_by = sort;
        q.descending = descending;
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        return ids(p);
    };
    BOOST_TEST(run(ClickEventSort::occurred_at, false)
               == "e01,e02,e03,e04,e05,e06,e07,e08,e09,e10,e11,e12");
    BOOST_TEST(run(ClickEventSort::occurred_at, true)
               == "e12,e11,e10,e09,e08,e07,e06,e05,e04,e03,e02,e01");
    BOOST_TEST(run(ClickEventSort::slug, false)
               == "e01,e02,e03,e04,e05,e06,e07,e08,e09,e10,e11,e12");
    BOOST_TEST(run(ClickEventSort::slug, true)
               == "e09,e10,e11,e12,e05,e06,e07,e08,e01,e02,e03,e04");
    BOOST_TEST(run(ClickEventSort::status_code, false)
               == "e01,e04,e07,e10,e02,e05,e08,e11,e03,e06,e09,e12");
    BOOST_TEST(run(ClickEventSort::status_code, true)
               == "e03,e06,e09,e12,e02,e05,e08,e11,e01,e04,e07,e10");
}

/// Primary-key ties resolve to `event_id` ascending, both directions.
inline void case_tie_break(Repo& repo)
{
    clear(repo);
    const std::vector<ClickEvent> batch = {
        ev("c", "s", "d.example", 302, 500),
        ev("a", "s", "d.example", 302, 500),
        ev("b", "s", "d.example", 302, 500),
    };
    BOOST_TEST(repo.InsertBatch(batch, nullptr));
    for (bool descending : {false, true}) {
        ClickEventQuery q;
        q.sort_by = ClickEventSort::occurred_at;
        q.descending = descending;
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(ids(p) == "a,b,c");
    }
}

/// `offset`/`limit` select a window; `total` is the full match count;
/// `offset > total` yields an empty window with the correct total.
inline void case_offset_limit_total(Repo& repo)
{
    clear(repo);
    seed12(repo);
    {
        ClickEventQuery q;
        q.sort_by = ClickEventSort::occurred_at;
        q.descending = false;
        q.offset = 10;
        q.limit = 5;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 12u);
        BOOST_TEST(ids(p) == "e11,e12");
    }
    {
        ClickEventQuery q;
        q.offset = 20;
        q.limit = 5;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 12u);
        BOOST_TEST(p.items.empty());
    }
}

/// A `limit` far above the cap returns at most `kMaxClickEventPageSize` rows
/// while `total` reports every match. Seeds 600 events via batches.
inline void case_limit_clamp(Repo& repo)
{
    clear(repo);
    for (int base = 0; base < 600; base += 200) {
        std::vector<ClickEvent> batch;
        for (int i = base; i < base + 200; ++i) {
            batch.push_back(
                ev("x" + std::to_string(i), "s", "d.example", 302, 10000 + i));
        }
        BOOST_TEST(repo.InsertBatch(batch, nullptr));
    }
    ClickEventQuery q;
    q.limit = 100000;
    ClickEventPage p;
    BOOST_TEST(repo.ListEvents(q, &p, nullptr));
    BOOST_TEST(p.total == 600u);
    BOOST_TEST(p.items.size() == kMaxClickEventPageSize);
    BOOST_TEST(p.items.size() == 500u);
}

/// `limit == 0` yields an empty window but the full `total`.
inline void case_limit_zero(Repo& repo)
{
    clear(repo);
    seed12(repo);
    ClickEventQuery q;
    q.limit = 0;
    ClickEventPage p;
    BOOST_TEST(repo.ListEvents(q, &p, nullptr));
    BOOST_TEST(p.items.empty());
    BOOST_TEST(p.total == 12u);
}

/// Empty `referrer`/`user_agent` round-trip to empty strings; `occurred_at` is
/// lossless to the second; only `client_id_hash` carries a client identifier.
inline void case_empty_text_fields_roundtrip(Repo& repo)
{
    clear(repo);
    const ClickEvent e =
        ev("empty1", "s", "d.example", 302, 1700000042LL, "", "");
    BOOST_TEST(repo.InsertBatch({e}, nullptr));
    ClickEventQuery q;
    q.slug = "s";
    q.limit = 10;
    ClickEventPage p;
    BOOST_TEST(repo.ListEvents(q, &p, nullptr));
    BOOST_REQUIRE_EQUAL(p.items.size(), 1u);
    BOOST_TEST(p.items[0].referrer.empty());
    BOOST_TEST(p.items[0].user_agent.empty());
    BOOST_TEST(p.items[0].link_id == "L-empty1");
    BOOST_TEST(p.items[0].client_id_hash == "hash-empty1");
    BOOST_TEST((p.items[0].occurred_at == ts(1700000042LL)));
}

/// SQL-injection strings in `slug`/`text` are inert (bound, never executed) and
/// the table survives.
inline void case_injection_inert(Repo& repo)
{
    clear(repo);
    seed12(repo);
    const std::string attack = "'; DROP TABLE click_events;--";
    {
        ClickEventQuery q;
        q.slug = attack;
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 0u);
        BOOST_TEST(p.items.empty());
    }
    {
        ClickEventQuery q;
        q.text = attack;
        q.limit = 100;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
        BOOST_TEST(p.total == 0u);
    }
    // The table survived: a normal query still returns every seeded event.
    ClickEventQuery q;
    q.limit = 100;
    ClickEventPage p;
    BOOST_TEST(repo.ListEvents(q, &p, nullptr));
    BOOST_TEST(p.total == 12u);
}

/// Defense in depth: a `text` filter at the cap is accepted; one over the cap
/// is a validation error (the arg layer already bounds it upstream).
inline void case_text_cap(Repo& repo)
{
    clear(repo);
    seed12(repo);
    {
        ClickEventQuery q;
        q.text = std::string(256, 'a');
        q.limit = 10;
        ClickEventPage p;
        BOOST_TEST(repo.ListEvents(q, &p, nullptr));
    }
    {
        ClickEventQuery q;
        q.text = std::string(257, 'a');
        q.limit = 10;
        ClickEventPage p;
        AnalyticsError err;
        BOOST_TEST(!repo.ListEvents(q, &p, &err));
        BOOST_TEST((err.code == AnalyticsErrorCode::validation));
    }
}

/// An `offset` of `SIZE_MAX` (above `LLONG_MAX`) succeeds with an empty window
/// and the correct `total`: the adapter clamps the offset before narrowing it
/// to the bound SQL integer, so it never wraps negative (which would make
/// PostgreSQL error and SQLite silently treat it as 0).
inline void case_offset_overflow(Repo& repo)
{
    clear(repo);
    seed12(repo);
    ClickEventQuery q;
    q.offset = std::numeric_limits<std::size_t>::max();
    q.limit = 100;
    ClickEventPage p;
    BOOST_TEST(repo.ListEvents(q, &p, nullptr));
    BOOST_TEST(p.items.empty());
    BOOST_TEST(p.total == 12u);
}

/// Inverted range and null page are validation errors; null `error` is safe.
inline void case_validation(Repo& repo)
{
    clear(repo);
    seed12(repo);
    {
        ClickEventQuery q;
        q.from = ts(300);
        q.to = ts(100);
        ClickEventPage p;
        AnalyticsError err;
        BOOST_TEST(!repo.ListEvents(q, &p, &err));
        BOOST_TEST((err.code == AnalyticsErrorCode::validation));
    }
    {
        ClickEventQuery q;
        AnalyticsError err;
        BOOST_TEST(!repo.ListEvents(q, nullptr, &err));
        BOOST_TEST((err.code == AnalyticsErrorCode::validation));
        BOOST_TEST(!repo.ListEvents(q, nullptr, nullptr));
    }
}

/// Runs every backend-neutral case against @p repo.
inline void run_all(Repo& repo)
{
    case_filters(repo);
    case_time_window(repo);
    case_text_wildcard_literal(repo);
    case_sort_all(repo);
    case_tie_break(repo);
    case_offset_limit_total(repo);
    case_limit_clamp(repo);
    case_limit_zero(repo);
    case_empty_text_fields_roundtrip(repo);
    case_injection_inert(repo);
    case_text_cap(repo);
    case_offset_overflow(repo);
    case_validation(repo);
}

}  // namespace ce_list_test

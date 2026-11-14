#define BOOST_TEST_MODULE InMemoryClickEventListEvents
#include <chrono>
#include <cstdint>
#include <string>

#include <boost/test/unit_test.hpp>

#include "url_shortener/analytics/click_event_query.hpp"
#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

using namespace url_shortener;
using analytics::ClickEvent;
using analytics::ClickEventPage;
using analytics::ClickEventQuery;
using analytics::ClickEventSort;

namespace
{

/// Builds a timestamp at @p secs seconds past the epoch for deterministic
/// ordering/boundary assertions.
analytics::Timestamp ts(long long secs)
{
    return analytics::Timestamp {std::chrono::seconds(secs)};
}

/// Builds a fully-populated click event. `client_id_hash` is the only client
/// identifier on `ClickEvent` (C6); it is derived from `id` so tests can assert
/// it round-trips.
ClickEvent ev(std::string id,
              std::string slug,
              std::string domain,
              std::uint16_t status,
              long long secs)
{
    ClickEvent e;
    e.event_id = std::move(id);
    e.slug = std::move(slug);
    e.domain = std::move(domain);
    e.status_code = status;
    e.occurred_at = ts(secs);
    e.client_id_hash = "hash-" + e.event_id;
    return e;
}

/// Populates @p repo with the deterministic 3-event fixture (distinct
/// slugs/domains/status/time). The repository holds a `std::mutex` and is
/// therefore neither copyable nor movable, so the fixture is filled in place
/// rather than returned by value.
void fill(storage::memory::InMemoryClickEventRepository& repo)
{
    repo.InsertBatch(
        {
            ev("e1", "alpha", "one.example", 302, 100),
            ev("e2", "beta", "two.example", 404, 200),
            ev("e3", "gamma", "three.example", 301, 300),
        },
        nullptr);
}

}  // namespace

/**
 * Scenario: an empty repository returns an empty page and a zero total.
 * Boundaries: no stored events.
 * On failure first check: that `ListEvents` reports success and does not leave
 * stale `items`/`total` from the out-param.
 */
BOOST_AUTO_TEST_CASE(empty_repo_returns_empty_page)
{
    storage::memory::InMemoryClickEventRepository repo;
    ClickEventQuery q;
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_TEST(page.items.empty());
    BOOST_TEST(page.total == 0u);
}

/**
 * Scenario: exact slug filter matches a single event.
 * Boundaries: slug equality is exact, not substring.
 */
BOOST_AUTO_TEST_CASE(filter_by_slug_exact)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;
    q.slug = "beta";
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_TEST(page.total == 1u);
    BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
    BOOST_TEST(page.items[0].event_id == "e2");
}

/**
 * Scenario: exact status-code filter.
 */
BOOST_AUTO_TEST_CASE(filter_by_status_code)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;
    q.status_code = static_cast<std::uint16_t>(404);
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_TEST(page.total == 1u);
    BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
    BOOST_TEST(page.items[0].status_code == 404);
}

/**
 * Scenario: the half-open time window includes `from` and excludes `to`.
 * Boundaries: event exactly at `from` is included; event exactly at `to` is
 * excluded.
 */
BOOST_AUTO_TEST_CASE(filter_time_window_from_inclusive_to_exclusive)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;
    q.from = ts(200);  // inclusive -> includes e2 (200)
    q.to = ts(300);  // exclusive -> excludes e3 (300)
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_TEST(page.total == 1u);
    BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
    BOOST_TEST(page.items[0].event_id == "e2");
}

/**
 * Scenario: case-insensitive `text` matches over slug OR domain.
 */
BOOST_AUTO_TEST_CASE(text_matches_slug_and_domain_case_insensitive)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);

    // Matches slug "alpha" via case-insensitive substring.
    {
        ClickEventQuery q;
        q.text = "LPH";
        ClickEventPage page;
        BOOST_TEST(repo.ListEvents(q, &page, nullptr));
        BOOST_TEST(page.total == 1u);
        BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
        BOOST_TEST(page.items[0].event_id == "e1");
    }
    // Matches domain "two.example" while slug "beta" does not contain it.
    {
        ClickEventQuery q;
        q.text = "TWO.";
        ClickEventPage page;
        BOOST_TEST(repo.ListEvents(q, &page, nullptr));
        BOOST_TEST(page.total == 1u);
        BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
        BOOST_TEST(page.items[0].event_id == "e2");
    }
}

/**
 * Scenario: default ordering is `occurred_at` descending (newest first).
 */
BOOST_AUTO_TEST_CASE(default_order_newest_first)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;  // sort_by=occurred_at, descending=true by default
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_REQUIRE_EQUAL(page.items.size(), 3u);
    BOOST_TEST(page.items[0].event_id == "e3");  // 300
    BOOST_TEST(page.items[1].event_id == "e2");  // 200
    BOOST_TEST(page.items[2].event_id == "e1");  // 100
}

/**
 * Scenario: each `sort_by` field in both directions.
 */
BOOST_AUTO_TEST_CASE(sort_by_every_field_both_directions)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);

    const auto ids = [&](ClickEventSort sort, bool desc)
    {
        ClickEventQuery q;
        q.sort_by = sort;
        q.descending = desc;
        ClickEventPage page;
        BOOST_TEST(repo.ListEvents(q, &page, nullptr));
        std::string out;
        for (const auto& e : page.items) {
            out += e.event_id;
        }
        return out;
    };

    // occurred_at: 100(e1) < 200(e2) < 300(e3)
    BOOST_TEST(ids(ClickEventSort::occurred_at, false) == "e1e2e3");
    BOOST_TEST(ids(ClickEventSort::occurred_at, true) == "e3e2e1");
    // slug: alpha(e1) < beta(e2) < gamma(e3)
    BOOST_TEST(ids(ClickEventSort::slug, false) == "e1e2e3");
    BOOST_TEST(ids(ClickEventSort::slug, true) == "e3e2e1");
    // status_code: 301(e3) < 302(e1) < 404(e2)
    BOOST_TEST(ids(ClickEventSort::status_code, false) == "e3e1e2");
    BOOST_TEST(ids(ClickEventSort::status_code, true) == "e2e1e3");
}

/**
 * Scenario: events tying on the primary sort key are ordered by `event_id`
 * ascending, regardless of sort direction (stable final tie-break).
 */
BOOST_AUTO_TEST_CASE(tie_broken_by_event_id_ascending)
{
    storage::memory::InMemoryClickEventRepository repo;
    // All share occurred_at=500; inserted out of event_id order.
    repo.InsertBatch(
        {
            ev("c", "s", "d", 302, 500),
            ev("a", "s", "d", 302, 500),
            ev("b", "s", "d", 302, 500),
        },
        nullptr);

    for (bool desc : {false, true}) {
        ClickEventQuery q;
        q.sort_by = ClickEventSort::occurred_at;
        q.descending = desc;
        ClickEventPage page;
        BOOST_TEST(repo.ListEvents(q, &page, nullptr));
        BOOST_REQUIRE_EQUAL(page.items.size(), 3u);
        BOOST_TEST(page.items[0].event_id == "a");
        BOOST_TEST(page.items[1].event_id == "b");
        BOOST_TEST(page.items[2].event_id == "c");
    }
}

/**
 * Scenario: `offset`/`limit` select a window while `total` reflects the full
 * match count.
 */
BOOST_AUTO_TEST_CASE(offset_limit_and_total)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;
    q.sort_by = ClickEventSort::occurred_at;
    q.descending = false;  // e1, e2, e3
    q.offset = 1;
    q.limit = 1;
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_TEST(page.total == 3u);
    BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
    BOOST_TEST(page.items[0].event_id == "e2");
}

/**
 * Scenario: a `limit` far above the cap is clamped to `kMaxClickEventPageSize`
 * (500) while `total` still reports every match.
 * Boundaries: 600 stored events, requested limit 100000.
 */
BOOST_AUTO_TEST_CASE(limit_above_cap_is_clamped_to_500)
{
    storage::memory::InMemoryClickEventRepository repo;
    std::vector<ClickEvent> batch;
    for (int i = 0; i < 600; ++i) {
        batch.push_back(ev("e" + std::to_string(i), "s", "d", 302, 1000 + i));
    }
    repo.InsertBatch(batch, nullptr);

    ClickEventQuery q;
    q.limit = 100000;
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_TEST(page.total == 600u);
    BOOST_TEST(page.items.size() == analytics::kMaxClickEventPageSize);
    BOOST_TEST(page.items.size() == 500u);
}

/**
 * Scenario: `limit == 0` yields an empty window but a correct `total`.
 */
BOOST_AUTO_TEST_CASE(limit_zero_returns_empty_items_with_total)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;
    q.limit = 0;
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_TEST(page.items.empty());
    BOOST_TEST(page.total == 3u);
}

/**
 * Scenario: an inverted range (`from > to`) is rejected as a validation error.
 * On failure first check: that both `from` and `to` are set before comparing.
 */
BOOST_AUTO_TEST_CASE(from_after_to_is_validation_error)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;
    q.from = ts(300);
    q.to = ts(100);
    ClickEventPage page;
    analytics::AnalyticsError err;
    BOOST_TEST(!repo.ListEvents(q, &page, &err));
    BOOST_TEST((err.code == analytics::AnalyticsErrorCode::validation));
}

/**
 * Scenario: a null `page` out-param is rejected as a validation error and is
 * safe with a null `error`.
 */
BOOST_AUTO_TEST_CASE(null_page_is_validation_error)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;
    analytics::AnalyticsError err;
    BOOST_TEST(!repo.ListEvents(q, nullptr, &err));
    BOOST_TEST((err.code == analytics::AnalyticsErrorCode::validation));
    // Null error out-param must not crash.
    BOOST_TEST(!repo.ListEvents(q, nullptr, nullptr));
}

/**
 * Scenario: a listed event exposes `client_id_hash` and no raw client
 * identifier (C6). The hash round-trips; the struct has no other client
 * identifier field to leak.
 */
BOOST_AUTO_TEST_CASE(listed_event_exposes_only_client_id_hash)
{
    storage::memory::InMemoryClickEventRepository repo;
    fill(repo);
    ClickEventQuery q;
    q.slug = "alpha";
    ClickEventPage page;
    BOOST_TEST(repo.ListEvents(q, &page, nullptr));
    BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
    BOOST_TEST(page.items[0].client_id_hash == "hash-e1");
}

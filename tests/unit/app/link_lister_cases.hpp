/**
 * @file link_lister_cases.hpp
 * @brief Backend- and implementation-agnostic seed set and expectation table
 * for the `ILinkLister` browse port.
 *
 * Shared verbatim by subtask 12 (`43_repository_link_lister.cpp`, repository
 * lister over in-memory and SQLite) and subtask 13 (legacy snapshot lister) so
 * both listers are proven to return identical results. The header produces
 * only `url_shortener::Link` seeds and `LinkListQuery` -> expected-slug cases;
 * it includes no storage headers and performs no seeding itself, so each test
 * seeds its own store (through the 02.0 codec) exactly as its lister requires.
 *
 * The seeds deliberately cover one link of every lifecycle status (active,
 * disabled, expired, soft-deleted) plus tags and campaign names, and their
 * `created_at`/`updated_at` orderings differ so the sort cases distinguish the
 * two timestamp fields. Expiry uses a real past wall-clock timestamp because
 * `resolveLinkStatus`/`isExpired` compare against the system clock, not a
 * `ManualClock`.
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <url_shortener/app/link_lister.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>

namespace link_lister_cases
{

/**
 * @brief Formats a whole-second offset from the epoch as an RFC 3339 Zulu
 * string.
 *
 * Whole seconds only, so a value survives the SQLite backend's
 * epoch-second storage and round-trips byte-identically on every backend.
 * @param seconds Offset from the Unix epoch, in seconds.
 * @return The RFC 3339 UTC string for that instant.
 */
inline std::string lister_case_timestamp(long long seconds)
{
    return url_shortener::formatTimestamp(
        std::chrono::system_clock::time_point {}
        + std::chrono::seconds(seconds));
}

/**
 * @brief Builds the canonical seed set: six links spanning every lifecycle
 * status, with tags and campaign names, and interleaved creation/update
 * orderings.
 *
 * `created_at` and `updated_at` are returned as RFC 3339 strings; a test that
 * needs the `CreateLink` `now` timestamp (and a distinct `updated_at`) parses
 * them back with `parseRfc3339Zulu`. Status is encoded exactly as the codec
 * persists it: `enabled`, `expires_at`, and `deleted_at` drive
 * `resolveLinkStatus`.
 *
 * @return The seed links in an intentionally unsorted creation order.
 */
inline std::vector<url_shortener::Link> seed_links()
{
    using url_shortener::Link;

    // A timestamp far in the real past (2001-09-09) so the expired link is
    // expired regardless of when the test runs; and in the future (2036) so
    // the future-expiry link stays active. The future value is kept below
    // INT32_MAX seconds because the SQLite backend reads INTEGER columns as a
    // 32-bit int (sqlite_session_factory GetOptLL), so a larger epoch would
    // overflow on read.
    const std::string past_expiry = lister_case_timestamp(1000000000LL);
    const std::string future_expiry = lister_case_timestamp(2100000000LL);

    std::vector<Link> links;

    {
        Link l;
        l.id = "id-alpha";
        l.slug = "alpha";
        l.target_url = "https://alpha.example.com/path";
        l.created_at = lister_case_timestamp(50);
        l.updated_at = lister_case_timestamp(50);
        l.enabled = true;  // active
        l.tags = {"red", "blue"};
        Link::Campaign c;
        c.name = "spring";
        l.campaign = c;
        links.push_back(std::move(l));
    }
    {
        Link l;
        l.id = "id-bravo";
        l.slug = "bravo";
        l.target_url = "https://bravo.example.com";
        l.created_at = lister_case_timestamp(10);
        l.updated_at = lister_case_timestamp(90);
        l.enabled = false;  // disabled
        l.tags = {"red"};
        links.push_back(std::move(l));
    }
    {
        Link l;
        l.id = "id-charlie";
        l.slug = "charlie";
        l.target_url = "https://charlie.example.com";
        l.created_at = lister_case_timestamp(30);
        l.updated_at = lister_case_timestamp(20);
        l.enabled = true;
        l.expires_at = past_expiry;  // expired
        l.tags = {"blue"};
        Link::Campaign c;
        c.name = "summer";
        l.campaign = c;
        links.push_back(std::move(l));
    }
    {
        Link l;
        l.id = "id-delta";
        l.slug = "delta";
        l.target_url = "https://delta.example.com/ALPHA";
        l.created_at = lister_case_timestamp(70);
        l.updated_at = lister_case_timestamp(40);
        l.enabled = true;
        l.deleted_at = lister_case_timestamp(75);  // soft-deleted
        l.tags = {"green"};
        links.push_back(std::move(l));
    }
    {
        Link l;
        l.id = "id-echo";
        l.slug = "echo";
        l.target_url = "https://echo.example.com";
        l.created_at = lister_case_timestamp(20);
        l.updated_at = lister_case_timestamp(80);
        l.enabled = true;
        l.expires_at = future_expiry;  // active (not yet expired)
        Link::Campaign c;
        c.name = "spring";
        l.campaign = c;
        links.push_back(std::move(l));
    }
    {
        Link l;
        l.id = "id-foxtrot";
        l.slug = "foxtrot";
        l.target_url = "https://foxtrot.example.com";
        l.created_at = lister_case_timestamp(60);
        l.updated_at = lister_case_timestamp(10);
        l.enabled = true;  // active
        l.tags = {"red", "green"};
        links.push_back(std::move(l));
    }

    return links;
}

/**
 * @brief One browse case: a query and the exact ordered slugs and total it
 * must return against @ref seed_links on any `ILinkLister` implementation and
 * any backend.
 */
struct ListerCase
{
    ///< Human-readable case label (used in assertion messages).
    std::string name;

    ///< Query to run against a store seeded with @ref seed_links.
    url_shortener::app::LinkListQuery query;

    ///< Expected result slugs, in the exact order the lister must return.
    std::vector<std::string> expected_slugs;

    ///< Expected `LinkListPage::total` (filtered count before the
    ///< `offset`/`limit` window), independent of the window.
    std::size_t expected_total;
};

/**
 * @brief The full expectation table exercised against every lister/backend.
 *
 * Covers: default query (active only, Q9), each explicit status, `all`, free
 * text hitting slug and URL case-insensitively, tag and campaign filters,
 * every sort field in both directions, and pagination windows whose `total`
 * stays constant.
 *
 * @return The shared case list.
 */
inline std::vector<ListerCase> lister_cases()
{
    using url_shortener::app::LinkListQuery;
    using url_shortener::app::list_sort;
    using url_shortener::app::list_status;

    std::vector<ListerCase> cases;

    // Default query: active only, newest-first by created_at (Q9 default).
    {
        LinkListQuery q;  // all defaults
        cases.push_back(
            {"default_active_desc", q, {"foxtrot", "alpha", "echo"}, 3});
    }

    // Explicit per-status filters (sorted by slug ascending for stability).
    {
        LinkListQuery q;
        q.status = list_status::active;
        q.sort = list_sort::slug;
        q.descending = false;
        cases.push_back({"status_active", q, {"alpha", "echo", "foxtrot"}, 3});
    }
    {
        LinkListQuery q;
        q.status = list_status::disabled;
        cases.push_back({"status_disabled", q, {"bravo"}, 1});
    }
    {
        LinkListQuery q;
        q.status = list_status::expired;
        cases.push_back({"status_expired", q, {"charlie"}, 1});
    }
    {
        LinkListQuery q;
        q.status = list_status::deleted;
        cases.push_back({"status_deleted", q, {"delta"}, 1});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::slug;
        q.descending = false;
        cases.push_back(
            {"status_all",
             q,
             {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot"},
             6});
    }

    // Free text: "Alpha" (mixed case) matches the "alpha" slug and the
    // "ALPHA" segment of delta's URL, case-insensitively.
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.q = "Alpha";
        q.sort = list_sort::slug;
        q.descending = false;
        cases.push_back({"q_slug_and_url_ci", q, {"alpha", "delta"}, 2});
    }

    // Tag filters (exact match).
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.tag = "red";
        q.sort = list_sort::slug;
        q.descending = false;
        cases.push_back({"tag_red", q, {"alpha", "bravo", "foxtrot"}, 3});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.tag = "green";
        q.sort = list_sort::slug;
        q.descending = false;
        cases.push_back({"tag_green", q, {"delta", "foxtrot"}, 2});
    }

    // Campaign filters (exact name match).
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.campaign = "spring";
        q.sort = list_sort::slug;
        q.descending = false;
        cases.push_back({"campaign_spring", q, {"alpha", "echo"}, 2});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.campaign = "summer";
        cases.push_back({"campaign_summer", q, {"charlie"}, 1});
    }

    // Sort every field, both directions (status all).
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::slug;
        q.descending = false;
        cases.push_back(
            {"sort_slug_asc",
             q,
             {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot"},
             6});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::slug;
        q.descending = true;
        cases.push_back(
            {"sort_slug_desc",
             q,
             {"foxtrot", "echo", "delta", "charlie", "bravo", "alpha"},
             6});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::created_at;
        q.descending = false;
        cases.push_back(
            {"sort_created_asc",
             q,
             {"bravo", "echo", "charlie", "alpha", "foxtrot", "delta"},
             6});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::created_at;
        q.descending = true;
        cases.push_back(
            {"sort_created_desc",
             q,
             {"delta", "foxtrot", "alpha", "charlie", "echo", "bravo"},
             6});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::updated_at;
        q.descending = false;
        cases.push_back(
            {"sort_updated_asc",
             q,
             {"foxtrot", "charlie", "delta", "alpha", "echo", "bravo"},
             6});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::updated_at;
        q.descending = true;
        cases.push_back(
            {"sort_updated_desc",
             q,
             {"bravo", "echo", "alpha", "delta", "charlie", "foxtrot"},
             6});
    }

    // Pagination windows: `total` stays 6 regardless of offset/limit.
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::slug;
        q.descending = false;
        q.offset = 2;
        q.limit = 2;
        cases.push_back({"window_offset_limit", q, {"charlie", "delta"}, 6});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::slug;
        q.descending = false;
        q.offset = 0;
        q.limit = 1;
        cases.push_back({"window_limit_one", q, {"alpha"}, 6});
    }
    {
        LinkListQuery q;
        q.status = list_status::all;
        q.sort = list_sort::slug;
        q.descending = false;
        q.offset = 10;
        q.limit = 5;
        cases.push_back({"window_offset_beyond", q, {}, 6});
    }

    return cases;
}

}  // namespace link_lister_cases

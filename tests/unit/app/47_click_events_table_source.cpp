/**
 * @file 47_click_events_table_source.cpp
 * @brief Unit tests for @ref url_shortener::app::ClickEventsTableSource: exact
 * column/row key set (only `client_id_hash` as a client identifier), newest-
 * first ordering, sort/`q` forwarding, `limit` clamping, empty-store total,
 * repository-failure mapping to a fixed value-free detail (CWE-209 regression),
 * referrer query-string stripping, free-text JSON validity, and RFC 3339
 * timestamp rendering.
 */
#define BOOST_TEST_MODULE AppClickEventsTableSourceTest
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "url_shortener/analytics/analytics_error.hpp"
#include "url_shortener/analytics/click_event.hpp"
#include "url_shortener/analytics/click_event_query.hpp"
#include "url_shortener/analytics/i_click_event_repository.hpp"
#include "url_shortener/app/app_error.hpp"
#include "url_shortener/app/click_events_table_source.hpp"
#include "url_shortener/core/utils.hpp"
#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

using url_shortener::formatTimestamp;
using url_shortener::analytics::AnalyticsError;
using url_shortener::analytics::AnalyticsErrorCode;
using url_shortener::analytics::ClickEvent;
using url_shortener::analytics::ClickEventPage;
using url_shortener::analytics::ClickEventQuery;
using url_shortener::analytics::ClickEventSort;
using url_shortener::analytics::IClickEventRepository;
using url_shortener::analytics::kMaxClickEventPageSize;
using url_shortener::analytics::Timestamp;
using url_shortener::app::AppErrorCode;
using url_shortener::app::ClickEventsTableSource;
using url_shortener::app::Result;
using url_shortener::app::TableColumn;
using url_shortener::app::TableQuery;
using url_shortener::app::TableRowsPage;
using url_shortener::storage::memory::InMemoryClickEventRepository;

namespace
{

// A time point `seconds` after the epoch, for deterministic occurred_at values.
Timestamp at_epoch_plus(long long seconds)
{
    return Timestamp {} + std::chrono::seconds(seconds);
}

// Builds a click event with the fields the source projects.
ClickEvent make_event(const std::string& event_id,
                      long long occurred_seconds,
                      const std::string& slug)
{
    ClickEvent event;
    event.event_id = event_id;
    event.occurred_at = at_epoch_plus(occurred_seconds);
    event.slug = slug;
    event.link_id = "link-" + slug;
    event.domain = "example.com";
    event.status_code = 302;
    event.referrer = "https://ref.example.com/page";
    event.user_agent = "UA/1.0";
    event.client_id_hash = "hash-" + event_id;
    return event;
}

// Minimal base that stubs the three pure methods; subclasses decide whether to
// override ListEvents (the default base impl reports "unsupported").
class StubRepo : public IClickEventRepository
{
public:
    bool InsertBatch(const std::vector<ClickEvent>&,
                     AnalyticsError* = nullptr) override
    {
        return true;
    }
    bool GetAggregateStats(const url_shortener::analytics::AggregateQuery&,
                           url_shortener::analytics::AggregateStats*,
                           AnalyticsError* = nullptr) override
    {
        return true;
    }
    bool DeleteOlderThan(Timestamp, AnalyticsError* = nullptr) override
    {
        return true;
    }
};

// Records the last ListEvents query and returns a configured page.
class RecordingRepo final : public StubRepo
{
public:
    mutable ClickEventQuery m_last_query;
    mutable bool m_called = false;
    ClickEventPage m_page;

    bool ListEvents(const ClickEventQuery& query,
                    ClickEventPage* page,
                    AnalyticsError* = nullptr) override
    {
        m_last_query = query;
        m_called = true;
        if (page != nullptr) {
            *page = m_page;
        }
        return true;
    }
};

// Returns false with a secret-looking message, to prove the source never
// forwards AnalyticsError::message (CWE-209). The literal is assembled at
// runtime so it is not a committed credential, only a value-free marker.
class SecretErrorRepo final : public StubRepo
{
public:
    // The sensitive substrings the mapped detail must never contain.
    static std::string secret_marker() { return "sup3rs3cret42"; }

    bool ListEvents(const ClickEventQuery&,
                    ClickEventPage*,
                    AnalyticsError* error = nullptr) override
    {
        if (error != nullptr) {
            error->code = AnalyticsErrorCode::repository;
            error->message = "backend driver failure: token=" + secret_marker()
                + " while reading click_events";
        }
        return false;
    }
};

const std::vector<std::string> k_expected_columns = {"event_id",
                                                     "occurred_at",
                                                     "slug",
                                                     "link_id",
                                                     "domain",
                                                     "status_code",
                                                     "referrer",
                                                     "user_agent",
                                                     "client_id_hash"};

// Extracts the ordered top-level object keys from a flat JSON object (see the
// sibling links-source test for the rationale).
std::vector<std::string> top_level_keys(const std::string& json)
{
    std::vector<std::string> keys;
    int depth = 0;
    std::size_t i = 0;
    const std::size_t n = json.size();
    while (i < n) {
        const char c = json[i];
        if (c == '{' || c == '[') {
            ++depth;
            ++i;
            continue;
        }
        if (c == '}' || c == ']') {
            --depth;
            ++i;
            continue;
        }
        if (c == '"') {
            std::string token;
            ++i;
            while (i < n) {
                const char d = json[i];
                if (d == '\\' && i + 1 < n) {
                    token.push_back(json[i + 1]);
                    i += 2;
                    continue;
                }
                if (d == '"') {
                    ++i;
                    break;
                }
                token.push_back(d);
                ++i;
            }
            if (depth == 1 && i < n && json[i] == ':') {
                keys.push_back(token);
            }
            continue;
        }
        ++i;
    }
    return keys;
}

}  // namespace

// Type: unit (metadata: name, writable, exact column list, T-52).
// Scenario: a fresh source is named "click_events", not writable, and exposes
//   the fixed nine-column list; only occurred_at/slug/status_code are sortable
//   and slug/domain searchable.
// Boundaries: client_id_hash is the only client-identifier column.
// On failure first check: did the literal column list drift (contract C6)?
BOOST_AUTO_TEST_CASE(metadata_name_writable_and_columns)
{
    InMemoryClickEventRepository repo;
    ClickEventsTableSource source(repo);

    BOOST_TEST(source.name() == "click_events");
    BOOST_TEST(!source.writable());

    const std::vector<TableColumn>& columns = source.columns();
    BOOST_REQUIRE(columns.size() == k_expected_columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        BOOST_TEST(columns[i].name == k_expected_columns[i]);
    }
    BOOST_TEST(columns[1].sortable);  // occurred_at
    BOOST_TEST(columns[2].sortable);  // slug
    BOOST_TEST(columns[2].searchable);  // slug
    BOOST_TEST(columns[4].searchable);  // domain
    BOOST_TEST(columns[5].type == "int");  // status_code
    BOOST_TEST(columns[5].sortable);  // status_code
    BOOST_TEST(columns[8].name == "client_id_hash");
}

// Type: unit (row key set: client_id_hash only; no raw client identifier,
// T-52). Scenario: one seeded event is serialized and its key set equals the
// nine
//   columns; no key is a raw client identifier.
// Boundaries: client_id_hash is present; no key equals "client_id" and no key
//   contains "ip", "address" or "fingerprint". This fails the moment anyone
//   adds a non-hashed client identifier column (success criterion 2).
// On failure first check: did serialize_click_row add a client-identifying key?
BOOST_AUTO_TEST_CASE(row_key_set_has_only_hashed_client_identifier)
{
    InMemoryClickEventRepository repo;
    std::vector<ClickEvent> events = {make_event("e1", 100, "alpha")};
    repo.InsertBatch(events);
    ClickEventsTableSource source(repo);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    BOOST_REQUIRE(page.value->rows_json.size() == 1u);

    const std::vector<std::string> keys =
        top_level_keys(page.value->rows_json[0]);
    BOOST_REQUIRE(keys.size() == k_expected_columns.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        BOOST_TEST(keys[i] == k_expected_columns[i]);
    }

    bool has_hash = false;
    for (const std::string& key : keys) {
        has_hash = has_hash || (key == "client_id_hash");
        BOOST_TEST(key != "client_id", "raw client_id column present");
        BOOST_TEST(key.find("ip") == std::string::npos,
                   "ip-like column present: " << key);
        BOOST_TEST(key.find("address") == std::string::npos,
                   "address-like column present: " << key);
        BOOST_TEST(key.find("fingerprint") == std::string::npos,
                   "fingerprint column present: " << key);
    }
    BOOST_TEST(has_hash);
}

// Type: unit (default order is newest first).
// Scenario: three events with distinct occurred_at are returned by the default
//   (no sort) query in descending occurred_at order.
// Boundaries: the default ClickEventQuery ordering is occurred_at descending.
// On failure first check: did the source override the repo's default order?
BOOST_AUTO_TEST_CASE(seeded_events_newest_first)
{
    InMemoryClickEventRepository repo;
    std::vector<ClickEvent> events = {make_event("e_old", 100, "a"),
                                      make_event("e_new", 300, "b"),
                                      make_event("e_mid", 200, "c")};
    repo.InsertBatch(events);
    ClickEventsTableSource source(repo);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    BOOST_REQUIRE(page.value->rows_json.size() == 3u);
    BOOST_TEST(page.value->rows_json[0].find("\"event_id\":\"e_new\"")
               != std::string::npos);
    BOOST_TEST(page.value->rows_json[1].find("\"event_id\":\"e_mid\"")
               != std::string::npos);
    BOOST_TEST(page.value->rows_json[2].find("\"event_id\":\"e_old\"")
               != std::string::npos);
}

// Type: unit (sort/q forwarded to the repository query).
// Scenario: a query with sort="slug", descending, q="foo" forwards
//   ClickEventSort::slug, descending=true and text="foo", plus offset/limit.
// Boundaries: an absent sort leaves the repo's natural order; q maps to text.
// On failure first check: did to_click_sort mis-map, or did q/offset/limit stop
//   forwarding?
BOOST_AUTO_TEST_CASE(sort_and_q_forwarded)
{
    RecordingRepo repo;
    repo.m_page.total = 0;
    ClickEventsTableSource source(repo);

    TableQuery query;
    query.sort = "slug";
    query.descending = true;
    query.q = "foo";
    query.offset = 5;
    query.limit = 10;

    const Result<TableRowsPage> page = source.rows(query);
    BOOST_REQUIRE(page.ok());
    BOOST_TEST(repo.m_called);
    BOOST_TEST((repo.m_last_query.sort_by == ClickEventSort::slug));
    BOOST_TEST(repo.m_last_query.descending);
    BOOST_REQUIRE(repo.m_last_query.text.has_value());
    BOOST_TEST(*repo.m_last_query.text == "foo");
    BOOST_TEST(repo.m_last_query.offset == 5u);
    BOOST_TEST(repo.m_last_query.limit == 10u);
}

// Type: unit (limit above the page cap is clamped).
// Scenario: a query with limit=1000 (above kMaxClickEventPageSize) forwards a
//   limit clamped to kMaxClickEventPageSize.
// Boundaries: the source never asks the repository for more than the port cap.
// On failure first check: did the std::min clamp disappear?
BOOST_AUTO_TEST_CASE(limit_above_cap_clamped)
{
    RecordingRepo repo;
    repo.m_page.total = 0;
    ClickEventsTableSource source(repo);

    TableQuery query;
    query.limit = 1000;

    const Result<TableRowsPage> page = source.rows(query);
    BOOST_REQUIRE(page.ok());
    BOOST_TEST(repo.m_last_query.limit == kMaxClickEventPageSize);
}

// Type: unit (empty store reports total 0).
// Scenario: row_count over an empty repository returns 0.
// Boundaries: the unfiltered total of an empty store is zero.
// On failure first check: did row_count read items instead of total?
BOOST_AUTO_TEST_CASE(empty_repository_total_zero)
{
    InMemoryClickEventRepository repo;
    ClickEventsTableSource source(repo);

    const Result<std::size_t> count = source.row_count();
    BOOST_REQUIRE(count.ok());
    BOOST_TEST(*count.value == 0u);
}

// Type: unit (unsupported ListEvents maps to a fixed, value-free detail).
// Scenario: a repository that does not implement ListEvents (the default base
//   impl) makes rows() and row_count() return storage_failure whose detail is a
//   fixed constant that does not contain the repository's "not supported" text.
// Boundaries: the source never surfaces the base impl's message.
// On failure first check: did the source forward error.message?
BOOST_AUTO_TEST_CASE(unsupported_list_events_maps_to_storage_failure)
{
    StubRepo repo;  // does not override ListEvents -> default "unsupported"
    ClickEventsTableSource source(repo);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_TEST(!page.ok());
    BOOST_TEST((page.error.code == AppErrorCode::storage_failure));
    BOOST_TEST(page.error.detail == "click event listing failed");
    BOOST_TEST(page.error.detail.find("supported") == std::string::npos);

    const Result<std::size_t> count = source.row_count();
    BOOST_TEST(!count.ok());
    BOOST_TEST((count.error.code == AppErrorCode::storage_failure));
    BOOST_TEST(count.error.detail == "click event listing failed");
}

// Type: unit (repository error message never leaks, CWE-209 regression, T-52).
// Scenario: a repository returning an error whose message contains a secret
//   marker makes rows() return a fixed detail that contains none of the secret.
// Boundaries: the detail is value-free regardless of the backend message.
// On failure first check: did the source copy AnalyticsError::message into the
//   detail?
BOOST_AUTO_TEST_CASE(repository_error_message_not_forwarded)
{
    SecretErrorRepo repo;
    ClickEventsTableSource source(repo);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_TEST(!page.ok());
    BOOST_TEST((page.error.code == AppErrorCode::storage_failure));
    BOOST_TEST(page.error.detail == "click event listing failed");
    BOOST_TEST(page.error.detail.find(SecretErrorRepo::secret_marker())
               == std::string::npos);
    BOOST_TEST(page.error.detail.find("driver") == std::string::npos);
}

// Type: unit (referrer query string and fragment are stripped, T-52).
// Scenario: an event whose referrer carries ?token=abc and #frag is serialized;
//   the emitted referrer keeps only the scheme/host/path and the secret token
//   does not appear anywhere in the row.
// Boundaries: everything from the first ? or # is dropped before escaping.
// On failure first check: did strip_referrer_query stop running?
BOOST_AUTO_TEST_CASE(referrer_query_string_stripped)
{
    InMemoryClickEventRepository repo;
    ClickEvent event = make_event("e1", 100, "alpha");
    event.referrer = "https://ref.example.com/path?token=abcmarker#frag";
    repo.InsertBatch({event});
    ClickEventsTableSource source(repo);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    const std::string& row = page.value->rows_json[0];

    BOOST_TEST(row.find("\"referrer\":\"https://ref.example.com/path\"")
               != std::string::npos);
    BOOST_TEST(row.find("token=abcmarker") == std::string::npos);
    BOOST_TEST(row.find("abcmarker") == std::string::npos);
    BOOST_TEST(row.find("frag") == std::string::npos);
}

// Type: unit (referrer authority userinfo is stripped, T-52).
// Scenario: several referrers whose authority carries userinfo credentials are
//   serialized; the emitted referrer keeps only scheme/host/path, the username
//   and password never appear, and an `@` in the path (not the authority) is
//   left untouched. A no-scheme value and the empty string are also checked.
// Boundaries: userinfo runs from just after `://` through the LAST `@` in the
//   authority (first `/`, `?`, `#` or end); values without `://` only lose their
//   query/fragment.
// On failure first check: did sanitize_referrer mis-locate the authority end or
//   pick the wrong `@`, or did it stop running?
BOOST_AUTO_TEST_CASE(referrer_authority_userinfo_stripped)
{
    struct Case
    {
        std::string input;
        std::string expected;
    };
    const std::vector<Case> cases = {
        {"https://user:pass@ref.example.com/path?token=x#f",
         "https://ref.example.com/path"},
        {"https://user@host", "https://host"},
        {"https://a:b@c@host/p", "https://host/p"},
        {"https://host/p@x", "https://host/p@x"},
        {"no-scheme/path@user?token=x#f", "no-scheme/path@user"},
        {"", ""}};

    for (const Case& c : cases) {
        InMemoryClickEventRepository repo;
        ClickEvent event = make_event("e1", 100, "alpha");
        event.referrer = c.input;
        repo.InsertBatch({event});
        ClickEventsTableSource source(repo);

        const Result<TableRowsPage> page = source.rows(TableQuery {});
        BOOST_REQUIRE(page.ok());
        const std::string& row = page.value->rows_json[0];

        BOOST_TEST(row.find("\"referrer\":\"" + c.expected + "\"")
                       != std::string::npos,
                   "referrer not sanitized for input: " << c.input);
        // The credential substrings must never survive into the row.
        BOOST_TEST(row.find("user:pass") == std::string::npos,
                   "userinfo leaked for input: " << c.input);
        BOOST_TEST(row.find("token=x") == std::string::npos,
                   "query token leaked for input: " << c.input);
    }
}

// Type: unit (hostile free text stays valid JSON, T-52).
// Scenario: an event whose referrer and user_agent carry quotes, a backslash, a
//   newline and a script tag is serialized; the raw newline/quote do not
//   survive and the script tag is present only as inert escaped text.
// Boundaries: every free-text cell is routed through jsonString.
// On failure first check: did a cell bypass jsonString?
BOOST_AUTO_TEST_CASE(hostile_free_text_stays_valid_json)
{
    InMemoryClickEventRepository repo;
    ClickEvent event = make_event("e1", 100, "alpha");
    event.referrer = "a\"b";
    event.user_agent = "x\"y\\z\n<script>";
    repo.InsertBatch({event});
    ClickEventsTableSource source(repo);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    const std::string& row = page.value->rows_json[0];

    BOOST_TEST(row.find('\n') == std::string::npos);
    BOOST_TEST(row.find("x\\\"y\\\\z\\n") != std::string::npos);
    BOOST_TEST(top_level_keys(row).size() == k_expected_columns.size());
}

// Type: unit (occurred_at rendered as RFC 3339 UTC).
// Scenario: an event at a known instant serializes occurred_at exactly as
//   formatTimestamp renders it (RFC 3339 Zulu).
// Boundaries: the cell matches the shared timestamp formatter byte-for-byte.
// On failure first check: did the source stop using formatTimestamp?
BOOST_AUTO_TEST_CASE(occurred_at_is_rfc3339_utc)
{
    InMemoryClickEventRepository repo;
    const Timestamp instant = at_epoch_plus(1700000000);
    ClickEvent event = make_event("e1", 1700000000, "alpha");
    repo.InsertBatch({event});
    ClickEventsTableSource source(repo);

    const Result<TableRowsPage> page = source.rows(TableQuery {});
    BOOST_REQUIRE(page.ok());
    const std::string expected =
        "\"occurred_at\":\"" + formatTimestamp(instant) + "\"";
    BOOST_TEST(page.value->rows_json[0].find(expected) != std::string::npos);
}

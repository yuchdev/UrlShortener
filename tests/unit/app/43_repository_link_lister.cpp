/**
 * @file 43_repository_link_lister.cpp
 * @brief Unit tests for app::RepositoryLinkLister and the shared browse port
 * (subtask 12).
 *
 * Runs the implementation- and backend-agnostic case table
 * (`link_lister_cases.hpp`, reused unchanged by subtask 13) against the
 * repository lister over BOTH an in-memory repository and a SQLite repository
 * (built like `tests/contract/metadata/metadata_contract_common.hpp` but with a
 * unique database file per test so parallel ctest runs never collide). Each
 * backend must return byte-identical results.
 *
 * Also covers: every listed item serialized equals
 * `serializeLinkViewJson(GetLink(slug))` for the same store; `total` is stable
 * across pages; query-bound validation (`limit` 0/501, `q` 257 chars) yields
 * `invalid_field` naming the field only; an empty repository lists nothing; a
 * repository forced to `permanent_failure` surfaces `storage_failure` with a
 * fixed detail; a record with no `lnk_*` attributes lists as active per codec
 * tolerance; a record with a malformed `lnk_*` attribute is skipped (no crash,
 * no raw-attribute leak); and `serializeLinkListJson` shape for 0/1/2 items.
 */
#define BOOST_TEST_MODULE RepositoryLinkLister
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/expiring_link_cache.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/app/link_lister.hpp>
#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/app/repository_link_lister.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/storage/errors/repo_error.hpp>
#include <url_shortener/storage/i_metadata_repository.hpp>
#include <url_shortener/storage/inmemory/in_memory_metadata_repository.hpp>
#include <url_shortener/storage/models/create_link_request.hpp>
#include <url_shortener/storage/models/link_record.hpp>

// Reused for its SQLite repository wiring (SqlMetadataRepository,
// SqliteSessionFactory, SqliteSqlDialect, SqlMetadataRowMapper); this test
// builds its own uniquely-named SQLite database rather than the harness's
// fixed-name one.
#include "../../contract/metadata/metadata_contract_common.hpp"
#include "app_test_fakes.hpp"
#include "link_lister_cases.hpp"

using namespace url_shortener;

namespace
{

/**
 * @brief Owns a repository plus the service wiring a `RepositoryLinkLister`
 * needs, so all referenced objects outlive the lister for a test's duration.
 */
struct ListerFixture
{
    std::shared_ptr<::IMetadataRepository> repo;
    std::shared_ptr<ManualClock> clock;
    std::shared_ptr<app::ExpiringLinkCache> l1;
    std::shared_ptr<app::RepositoryLinkStore> store;
    test_fakes::FakeLinkStatsReader stats_reader;
    ServerConfig config;
    std::unique_ptr<app::LinkCommandService> service;
    std::unique_ptr<app::RepositoryLinkLister> lister;

    explicit ListerFixture(std::shared_ptr<::IMetadataRepository> repository)
        : repo(std::move(repository))
        , clock(std::make_shared<ManualClock>(
              std::chrono::system_clock::time_point {}))
        , l1(std::make_shared<app::ExpiringLinkCache>(*clock,
                                                      std::chrono::seconds(0)))
        , store(std::make_shared<app::RepositoryLinkStore>(
              repo, nullptr, l1, *clock))
        , config(test_fakes::makeTestConfig())
    {
        service = std::make_unique<app::LinkCommandService>(
            *store, stats_reader, config);
        lister = std::make_unique<app::RepositoryLinkLister>(repo, *service);
    }
};

/**
 * @brief Seeds @p repo with the shared seed links through the 02.0 codec.
 *
 * Creates each link with its `created_at` as the `now` timestamp, then (when
 * the seed's `updated_at` differs) re-stamps `updated_at` through `UpdateLink`
 * so the created/updated orderings differ for the sort cases.
 * @param repo Repository to seed in place.
 */
void seed_shared_links(::IMetadataRepository& repo)
{
    for (const Link& link : link_lister_cases::seed_links()) {
        ::CreateLinkRequest request;
        std::string error;
        BOOST_REQUIRE_MESSAGE(
            app::link_to_create_request(link, &request, &error),
            "encode create for " << link.slug << ": " << error);

        const auto created = parseRfc3339Zulu(link.created_at);
        BOOST_REQUIRE(created.has_value());

        ::RepoError repo_error = ::RepoError::none;
        BOOST_REQUIRE_MESSAGE(
            repo.CreateLink(request, link.id, *created, &repo_error),
            "create " << link.slug);

        if (link.updated_at != link.created_at) {
            ::RepoError get_error = ::RepoError::none;
            auto record = repo.GetByShortCode(link.slug, &get_error);
            BOOST_REQUIRE(record.has_value());
            const auto updated = parseRfc3339Zulu(link.updated_at);
            BOOST_REQUIRE(updated.has_value());
            record->updated_at = *updated;
            ::RepoError update_error = ::RepoError::none;
            BOOST_REQUIRE(repo.UpdateLink(*record, &update_error));
        }
    }
}

/**
 * @brief Extracts the slugs of a page's items in order.
 */
std::vector<std::string> slugs_of(const app::LinkListPage& page)
{
    std::vector<std::string> out;
    out.reserve(page.items.size());
    for (const auto& item : page.items) {
        out.push_back(item.slug);
    }
    return out;
}

/**
 * @brief Builds a unique SQLite database path so parallel test processes never
 * share a file.
 */
std::string unique_sqlite_path()
{
    static std::atomic<unsigned long long> counter {0};
    const auto n = counter.fetch_add(1);
    std::random_device rd;
    const auto nanos =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path()
        / ("url_shortener_lister_" + std::to_string(nanos) + "_"
           + std::to_string(rd()) + "_" + std::to_string(n) + ".sqlite3");
    std::filesystem::remove(path);
    return path.string();
}

/**
 * @brief Builds a fresh, empty SQLite-backed metadata repository with a unique
 * database file.
 */
std::shared_ptr<::IMetadataRepository> make_sqlite_repo()
{
    SqlConnectionConfig config;
    config.backend = SqlBackendKind::sqlite;
    config.dsn = unique_sqlite_path();
    return std::make_shared<SqlMetadataRepository>(
        std::make_shared<SqliteSessionFactory>(config),
        std::make_shared<SqliteSqlDialect>(),
        std::make_shared<SqlMetadataRowMapper>());
}

/**
 * @brief Runs the shared case table and the per-item GetLink parity check
 * against one backend.
 * @param backend Backend label for assertion messages.
 * @param repo Freshly-built empty repository for this backend.
 */
void run_shared_cases(const std::string& backend,
                      std::shared_ptr<::IMetadataRepository> repo)
{
    seed_shared_links(*repo);
    ListerFixture fx(repo);

    for (const auto& test_case : link_lister_cases::lister_cases()) {
        BOOST_TEST_CONTEXT(backend << " / " << test_case.name)
        {
            const auto result = fx.lister->list(test_case.query);
            BOOST_REQUIRE(result.ok());
            const auto& page = *result.value;

            BOOST_CHECK_EQUAL(page.total, test_case.expected_total);
            BOOST_CHECK_EQUAL(page.offset, test_case.query.offset);
            BOOST_CHECK_EQUAL(page.limit, test_case.query.limit);

            const auto actual = slugs_of(page);
            BOOST_REQUIRE_EQUAL(actual.size(), test_case.expected_slugs.size());
            for (std::size_t i = 0; i < actual.size(); ++i) {
                BOOST_CHECK_EQUAL(actual[i], test_case.expected_slugs[i]);
            }

            // Every listed item is byte-identical to a single-link read.
            for (const auto& item : page.items) {
                app::GetLinkQuery query;
                query.by = app::GetLinkBy::slug;
                query.value = item.slug;
                const auto got = fx.service->GetLink(query);
                BOOST_REQUIRE_MESSAGE(got.ok(), "GetLink(" << item.slug << ")");
                BOOST_CHECK_EQUAL(app::serializeLinkViewJson(item),
                                  app::serializeLinkViewJson(*got.value));
            }
        }
    }
}

/**
 * @brief Minimal `IMetadataRepository` whose `ListLinks` always fails with
 * `permanent_failure`; every other method is an inert stub.
 */
class PermanentFailureRepository final : public ::IMetadataRepository
{
public:
    bool CreateLink(const ::CreateLinkRequest&,
                    const std::string&,
                    const std::chrono::system_clock::time_point&,
                    ::RepoError* error) override
    {
        if (error != nullptr) {
            *error = ::RepoError::permanent_failure;
        }
        return false;
    }

    std::optional<::LinkRecord> GetByShortCode(const std::string&,
                                               ::RepoError*) const override
    {
        return std::nullopt;
    }

    std::optional<::LinkRecord> GetById(const std::string&,
                                        ::RepoError*) const override
    {
        return std::nullopt;
    }

    bool UpdateLink(const ::LinkRecord&, ::RepoError* error) override
    {
        if (error != nullptr) {
            *error = ::RepoError::permanent_failure;
        }
        return false;
    }

    bool DeleteLink(const std::string&, ::RepoError* error) override
    {
        if (error != nullptr) {
            *error = ::RepoError::permanent_failure;
        }
        return false;
    }

    std::vector<::LinkRecord> ListLinks(const ::ListLinksQuery&,
                                        ::RepoError* error) const override
    {
        if (error != nullptr) {
            *error = ::RepoError::permanent_failure;
        }
        return {};
    }

    bool Exists(const std::string&, ::RepoError* error) const override
    {
        if (error != nullptr) {
            *error = ::RepoError::permanent_failure;
        }
        return false;
    }
};

}  // namespace

/**
 * [Unit][App] The shared case table returns identical results over an
 * in-memory repository.
 *
 * Scenario:
 *   Given the canonical seed links in an in-memory repository.
 *   When each shared browse case runs.
 *   Then the slugs, order and total match the case's expectation and every
 *   item equals its GetLink projection.
 *
 * If this breaks, first check:
 *   - build_list_links_query status/tag/campaign predicate and q->text.
 *   - ViewOf matching GetLink's toView projection.
 */
BOOST_AUTO_TEST_CASE(shared_cases_inmemory)
{
    run_shared_cases("inmemory",
                     std::make_shared<::InMemoryMetadataRepository>());
}

/**
 * [Unit][App] The same shared case table returns identical results over a
 * SQLite repository, proving backend independence.
 *
 * Scenario:
 *   Given the canonical seed links in a uniquely-named SQLite repository.
 *   When each shared browse case runs.
 *   Then results are byte-identical to the in-memory backend.
 *
 * If this breaks, first check (re-run this test alone first — SQLite file
 * contention under -jN can masquerade as a logic failure):
 *   - the default ListLinksPage scan path being backend-agnostic.
 *   - codec attribute round-trip through the SQL row mapper.
 */
BOOST_AUTO_TEST_CASE(shared_cases_sqlite)
{
    run_shared_cases("sqlite", make_sqlite_repo());
}

/**
 * [Unit][App] limit 0 and limit 501 are rejected as invalid_field naming the
 * field only; q of 257 chars likewise.
 *
 * Scenario:
 *   Given a seeded in-memory lister.
 *   When a query with an out-of-range limit or oversized q is listed.
 *   Then the result is invalid_field with detail "limit"/"q" and no value.
 *
 * If this breaks, first check validate_link_list_query bounds and the fixed
 * field-name details.
 */
BOOST_AUTO_TEST_CASE(query_bounds_rejected)
{
    auto repo = std::make_shared<::InMemoryMetadataRepository>();
    seed_shared_links(*repo);
    ListerFixture fx(repo);

    {
        app::LinkListQuery q;
        q.limit = 0;
        const auto r = fx.lister->list(q);
        BOOST_REQUIRE(!r.ok());
        BOOST_CHECK_EQUAL(r.error.code, app::AppErrorCode::invalid_field);
        BOOST_CHECK_EQUAL(r.error.detail, "limit");
    }
    {
        app::LinkListQuery q;
        q.limit = app::link_list_max_limit + 1;
        const auto r = fx.lister->list(q);
        BOOST_REQUIRE(!r.ok());
        BOOST_CHECK_EQUAL(r.error.code, app::AppErrorCode::invalid_field);
        BOOST_CHECK_EQUAL(r.error.detail, "limit");
    }
    {
        app::LinkListQuery q;
        q.q = std::string(app::link_list_max_query_length + 1, 'x');
        const auto r = fx.lister->list(q);
        BOOST_REQUIRE(!r.ok());
        BOOST_CHECK_EQUAL(r.error.code, app::AppErrorCode::invalid_field);
        BOOST_CHECK_EQUAL(r.error.detail, "q");
    }
    {
        // Exactly at the bound is accepted.
        app::LinkListQuery q;
        q.status = app::list_status::all;
        q.limit = app::link_list_max_limit;
        q.q = std::string(app::link_list_max_query_length, 'x');
        const auto r = fx.lister->list(q);
        BOOST_REQUIRE(r.ok());
    }
}

/**
 * [Unit][App] An empty repository lists nothing with total 0.
 *
 * Scenario:
 *   Given an empty in-memory repository.
 *   When status=all is listed.
 *   Then items is empty and total is 0.
 *
 * If this breaks, first check ListLinksPage over an empty store and the
 * LinkListPage default total.
 */
BOOST_AUTO_TEST_CASE(empty_repository_lists_nothing)
{
    auto repo = std::make_shared<::InMemoryMetadataRepository>();
    ListerFixture fx(repo);

    app::LinkListQuery q;
    q.status = app::list_status::all;
    const auto r = fx.lister->list(q);
    BOOST_REQUIRE(r.ok());
    BOOST_CHECK(r.value->items.empty());
    BOOST_CHECK_EQUAL(r.value->total, 0u);
}

/**
 * [Unit][App] A repository that fails the scan surfaces storage_failure with a
 * fixed detail, never the driver error.
 *
 * Scenario:
 *   Given a repository whose ListLinks returns permanent_failure.
 *   When any query is listed.
 *   Then the result is storage_failure with detail "list failed".
 *
 * If this breaks, first check the ListLinksPage failure collapse in
 * RepositoryLinkLister::list and the fixed detail string.
 */
BOOST_AUTO_TEST_CASE(repository_failure_is_storage_failure)
{
    auto good = std::make_shared<::InMemoryMetadataRepository>();
    ListerFixture fx(good);  // service only; never consulted on failure
    auto failing = std::make_shared<PermanentFailureRepository>();
    app::RepositoryLinkLister lister(failing, *fx.service);

    app::LinkListQuery q;
    q.status = app::list_status::all;
    const auto r = lister.list(q);
    BOOST_REQUIRE(!r.ok());
    BOOST_CHECK_EQUAL(r.error.code, app::AppErrorCode::storage_failure);
    BOOST_CHECK_EQUAL(r.error.detail, "list failed");
}

/**
 * [Unit][App] A record written with no lnk_* attributes lists as active per
 * the codec's tolerant decode.
 *
 * Scenario:
 *   Given a record created directly with empty attributes and is_active=true.
 *   When status=active is listed.
 *   Then the record appears, with status active, matching its GetLink view.
 *
 * If this breaks, first check record_to_link's missing-attribute defaults and
 * the active-status predicate.
 */
BOOST_AUTO_TEST_CASE(record_without_attributes_lists_as_active)
{
    auto repo = std::make_shared<::InMemoryMetadataRepository>();
    ::CreateLinkRequest request;
    request.short_code = "plain";
    request.target_url = "https://plain.example.com";
    request.is_active = true;  // no lnk_* attributes at all
    ::RepoError error = ::RepoError::none;
    BOOST_REQUIRE(repo->CreateLink(
        request, "id-plain", std::chrono::system_clock::time_point {}, &error));

    ListerFixture fx(repo);
    app::LinkListQuery q;
    q.status = app::list_status::active;
    const auto r = fx.lister->list(q);
    BOOST_REQUIRE(r.ok());
    BOOST_REQUIRE_EQUAL(r.value->items.size(), 1u);
    BOOST_CHECK_EQUAL(r.value->items[0].slug, "plain");
    BOOST_CHECK(r.value->items[0].status == LinkStatus::active);

    app::GetLinkQuery get;
    get.value = "plain";
    const auto got = fx.service->GetLink(get);
    BOOST_REQUIRE(got.ok());
    BOOST_CHECK_EQUAL(app::serializeLinkViewJson(r.value->items[0]),
                      app::serializeLinkViewJson(*got.value));
}

/**
 * [Unit][App] A record with a malformed lnk_* attribute is skipped, not
 * surfaced, and does not crash or leak its raw attributes.
 *
 * Scenario:
 *   Given a valid "good" link and a record carrying an unsupported lnk_v
 *   schema value alongside it.
 *   When status=all is listed.
 *   Then only the decodable link is returned and total counts it alone.
 *
 * If this breaks, first check the extra_filter decode-fail skip in
 * build_list_links_query.
 */
BOOST_AUTO_TEST_CASE(undecodable_record_is_skipped)
{
    auto repo = std::make_shared<::InMemoryMetadataRepository>();

    // A decodable neighbor.
    ::CreateLinkRequest good;
    good.short_code = "good";
    good.target_url = "https://good.example.com";
    good.is_active = true;
    ::RepoError e1 = ::RepoError::none;
    BOOST_REQUIRE(repo->CreateLink(
        good, "id-good", std::chrono::system_clock::time_point {}, &e1));

    // A record with an unsupported schema version -> record_to_link fails.
    ::CreateLinkRequest broken;
    broken.short_code = "broken";
    broken.target_url = "https://broken.example.com";
    broken.is_active = true;
    broken.attributes[std::string(app::attr_schema)] = "999";
    ::RepoError e2 = ::RepoError::none;
    BOOST_REQUIRE(repo->CreateLink(
        broken, "id-broken", std::chrono::system_clock::time_point {}, &e2));

    ListerFixture fx(repo);
    app::LinkListQuery q;
    q.status = app::list_status::all;
    const auto r = fx.lister->list(q);
    BOOST_REQUIRE(r.ok());
    BOOST_REQUIRE_EQUAL(r.value->items.size(), 1u);
    BOOST_CHECK_EQUAL(r.value->items[0].slug, "good");
    BOOST_CHECK_EQUAL(r.value->total, 1u);
}

/**
 * [Unit][App] serializeLinkListJson emits the canonical shape for 0, 1 and 2
 * items.
 *
 * Scenario:
 *   Given pages of 0, 1 and 2 views.
 *   When serialized.
 *   Then the output is {"items":[...],"offset":N,"limit":N,"total":N} with
 *   each item equal to serializeLinkViewJson.
 *
 * If this breaks, first check the item join and the offset/limit/total tail.
 */
BOOST_AUTO_TEST_CASE(serialize_link_list_json_shape)
{
    auto repo = std::make_shared<::InMemoryMetadataRepository>();
    seed_shared_links(*repo);
    ListerFixture fx(repo);

    app::GetLinkQuery q_alpha;
    q_alpha.value = "alpha";
    app::GetLinkQuery q_bravo;
    q_bravo.value = "bravo";
    const auto alpha = fx.service->GetLink(q_alpha);
    const auto bravo = fx.service->GetLink(q_bravo);
    BOOST_REQUIRE(alpha.ok());
    BOOST_REQUIRE(bravo.ok());

    {
        app::LinkListPage page;
        page.offset = 0;
        page.limit = 0;
        page.total = 0;
        BOOST_CHECK_EQUAL(
            app::serializeLinkListJson(page),
            "{\"items\":[],\"offset\":0,\"limit\":0,\"total\":0}");
    }
    {
        app::LinkListPage page;
        page.items = {*alpha.value};
        page.offset = 3;
        page.limit = 10;
        page.total = 1;
        const std::string expected = "{\"items\":["
            + app::serializeLinkViewJson(*alpha.value)
            + "],\"offset\":3,\"limit\":10,\"total\":1}";
        BOOST_CHECK_EQUAL(app::serializeLinkListJson(page), expected);
    }
    {
        app::LinkListPage page;
        page.items = {*alpha.value, *bravo.value};
        page.offset = 0;
        page.limit = 50;
        page.total = 2;
        const std::string expected = "{\"items\":["
            + app::serializeLinkViewJson(*alpha.value) + ","
            + app::serializeLinkViewJson(*bravo.value)
            + "],\"offset\":0,\"limit\":50,\"total\":2}";
        BOOST_CHECK_EQUAL(app::serializeLinkListJson(page), expected);
    }
}

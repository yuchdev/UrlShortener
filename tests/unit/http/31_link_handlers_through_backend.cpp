/**
 * @file 31_link_handlers_through_backend.cpp
 * @brief Unit tests for subtask 02.0/09: the REST management handlers and the
 * `generateUniqueSlug` helper obtain their Link / slug-existence answers
 * through `app::LinkBackend::store()` instead of the legacy
 * `getLinkForRead` / `linkRepository().slugExists` free functions.
 *
 * Scenario:
 *   With a functional FakeLinkStore installed (the legacy `linkRepository()`
 *   singleton left empty), the real `RouterBuilder::buildApplicationRouter()`
 *   must satisfy every REST resource route — create, read, update, delete,
 *   enable/disable/restore, legacy no-window stats, and the qr/routing 501
 *   placeholders — out of the installed store, producing the exact bodies and
 *   statuses the REST contract locks in (C3). `generateUniqueSlug` must ask
 *   the installed store's `slugExists`. After `LinkBackend::reset()` the
 *   legacy path works again.
 *
 * Boundaries:
 *   - The fake is the only backing store; `url_shortener::linkRepository()`
 *     is asserted to stay empty, proving no request path read or wrote the
 *     legacy singleton.
 *   - `generateUniqueSlug` with a fake that reports every candidate as
 *     existing gives up after the bounded retries (mirrors 06_slug_collision).
 *   - A source-scanning guard proves no file under `src/http` or `src/cli`
 *     calls `store().create`/`store().update` (or a legacy direct-write
 *     helper) — every write stays upstream in `LinkCommandService`
 *     (threat model T-40).
 *
 * On failure first check:
 *   - Each router case runs under BackendFixture, which reset()s the holder
 *     before and after, so a leaked install from a prior case is the first
 *     suspect.
 *   - A handler still calling `getLinkForRead` / `linkRepository()` directly
 *     would make the legacy-empty assertions fail and/or the store call
 *     counters stay at zero.
 *   - The guard flagging a file means a new `store().create`/`store().update`
 *     slipped into the HTTP or CLI layer, bypassing LinkCommandService's
 *     URL/slug/SSRF validation (T-40).
 */
#define BOOST_TEST_MODULE LinkHandlersThroughBackend
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/http/request_handlers.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace bhttp = boost::beast::http;
namespace fs = std::filesystem;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::app::AppError;
using url_shortener::app::ILinkStore;
using url_shortener::app::LinkBackend;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouterBuilder;

namespace
{

/**
 * @brief Functional in-memory test double for app::ILinkStore that counts how
 * the REST handlers use the store, without touching any global singleton.
 *
 * Backed by two maps (by slug and by id) so create/read/update behave like a
 * real store. Counts @ref create, @ref findBySlug and @ref update calls so a
 * case can prove a route reached the installed store. Thread-hostile: use
 * from a single test thread only.
 */
class FakeLinkStore final : public ILinkStore
{
public:
    /**
     * @brief Directly seeds a link (bypassing validation) into both maps.
     * @param link Link to store, keyed by its slug and id.
     */
    void seed(const Link& link)
    {
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
    }

    /// @return Number of times @ref create was called.
    std::size_t createCount() const { return create_count_; }

    /// @return Number of times @ref findBySlug was called.
    std::size_t findCount() const { return find_count_; }

    /// @return Number of times @ref update was called.
    std::size_t updateCount() const { return update_count_; }

    bool create(const Link& link, AppError* error = nullptr) override
    {
        ++create_count_;
        if (by_slug_.count(link.slug) > 0) {
            if (error != nullptr) {
                error->code = url_shortener::app::AppErrorCode::slug_conflict;
                error->detail = "slug already in use";
            }
            return false;
        }
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
        return true;
    }

    std::optional<Link> findBySlug(const std::string& slug) const override
    {
        ++find_count_;
        const auto it = by_slug_.find(slug);
        if (it != by_slug_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<Link> findById(const std::string& id) const override
    {
        const auto it = by_id_.find(id);
        if (it != by_id_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool slugExists(const std::string& slug) const override
    {
        return by_slug_.count(slug) > 0;
    }

    void invalidateCache(const std::string& /*slug*/) override {}

    bool update(const Link& link, AppError* error = nullptr) override
    {
        ++update_count_;
        if (by_id_.count(link.id) == 0) {
            if (error != nullptr) {
                error->code = url_shortener::app::AppErrorCode::not_found;
                error->detail = "link not found";
            }
            return false;
        }
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
        return true;
    }

private:
    std::unordered_map<std::string, Link> by_slug_;
    std::unordered_map<std::string, Link> by_id_;
    mutable std::size_t find_count_ = 0;
    std::size_t create_count_ = 0;
    std::size_t update_count_ = 0;
};

/**
 * @brief A store whose @ref slugExists always reports the slug as taken, used
 * to drive `generateUniqueSlug` to exhaustion.
 */
class AlwaysExistsStore final : public ILinkStore
{
public:
    bool create(const Link& /*link*/, AppError* /*error*/ = nullptr) override
    {
        return false;
    }
    std::optional<Link> findBySlug(const std::string& /*slug*/) const override
    {
        return std::nullopt;
    }
    std::optional<Link> findById(const std::string& /*id*/) const override
    {
        return std::nullopt;
    }
    bool slugExists(const std::string& /*slug*/) const override { return true; }
    void invalidateCache(const std::string& /*slug*/) override {}
    bool update(const Link& /*link*/, AppError* /*error*/ = nullptr) override
    {
        return false;
    }
};

/**
 * @brief Clears any installed store before and after each case so cases cannot
 * leak an installation into one another.
 */
struct BackendFixture
{
    /// Ensures a clean (default) holder at the start of the case.
    BackendFixture() { LinkBackend::reset(); }

    /// Restores the default so a leaked fake never outlives the case.
    ~BackendFixture() { LinkBackend::reset(); }
};

/**
 * @brief Builds a request with Host + JSON content-type headers set.
 * @param method HTTP verb.
 * @param target Request target (path, optionally with query).
 * @param body Request body; defaults to an empty JSON object.
 * @return A prepared request.
 */
BeastRequest makeRequest(const bhttp::verb method,
                         const std::string& target,
                         const std::string& body = "{}")
{
    BeastRequest req {method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.set(bhttp::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();
    return req;
}

/**
 * @brief Minimal config allowing private targets so the test exercises the
 * store substitution, not SSRF rejection.
 * @return A ServerConfig for dispatch.
 */
ServerConfig testConfig()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.shortener_generated_slug_length = 8;
    return config;
}

/**
 * @brief Dispatches @p req through a freshly built application router.
 * @param req Request to dispatch.
 * @return The response produced by the router.
 */
boost::beast::http::response<boost::beast::http::string_body> dispatch(
    const BeastRequest& req)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, testConfig(), false);
}

/**
 * @brief Builds an active link for @p slug seeded directly into the fake.
 * @param slug Slug for the link.
 * @return A populated active link.
 */
Link makeLink(const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = url_shortener::currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    return link;
}

}  // namespace

/**
 * [Unit][Http] POST /api/v1/links creates through the installed store, never
 * the legacy repository, and returns the stored link (C3).
 *
 * Scenario:
 *   Given a FakeLinkStore installed and the legacy repository empty.
 *   When POST /api/v1/links creates a custom-slug link.
 *   Then the fake's create() runs exactly once, the legacy repository stays
 *   empty, and the 201 body equals the body a subsequent GET of the same slug
 *   produces (both serialize the stored link through the shared view codec).
 *
 * If this breaks, first check:
 *   - createLinkFromRequest routing through makeCommandService -> store().
 */
BOOST_FIXTURE_TEST_CASE(create_runs_through_backend, BackendFixture)
{
    auto store = std::make_shared<FakeLinkStore>();
    LinkBackend::install(store);

    const auto created = dispatch(makeRequest(
        bhttp::verb::post,
        "/api/v1/links",
        "{\"url\":\"https://example.com/new\",\"slug\":\"bk31create01\"}"));
    BOOST_TEST(created.result_int() == 201);
    BOOST_TEST(created.body().find("\"slug\":\"bk31create01\"")
               != std::string::npos);
    BOOST_TEST(store->createCount() == 1U);

    // The legacy singleton was never written.
    BOOST_TEST(
        !url_shortener::linkRepository().getBySlug("bk31create01").has_value());

    // C3: the create body reflects exactly the stored link — identical to a
    // GET of the same slug, which reads it back through the fake.
    const auto fetched =
        dispatch(makeRequest(bhttp::verb::get, "/api/v1/links/bk31create01"));
    BOOST_TEST(fetched.result_int() == 200);
    BOOST_TEST(created.body() == fetched.body());
}

/**
 * [Unit][Http] Read / update / delete / lifecycle routes resolve their link
 * through the installed store's findBySlug and persist through its update.
 *
 * Scenario:
 *   Given a FakeLinkStore seeded with one active link and the legacy
 *   repository empty.
 *   When GET, PATCH, enable/disable/restore and DELETE are dispatched.
 *   Then each read increments the fake's findBySlug, each mutation increments
 *   its update, the documented status bodies are returned, and the legacy
 *   repository is never touched.
 *
 * If this breaks, first check:
 *   - the lifecycle/update/delete handlers still route through
 *     makeCommandService (not getLinkForRead / updateLinkAndInvalidateCache).
 */
BOOST_FIXTURE_TEST_CASE(read_update_lifecycle_through_backend, BackendFixture)
{
    auto store = std::make_shared<FakeLinkStore>();
    store->seed(makeLink("bk31rw01"));
    LinkBackend::install(store);

    const auto get =
        dispatch(makeRequest(bhttp::verb::get, "/api/v1/links/bk31rw01"));
    BOOST_TEST(get.result_int() == 200);
    BOOST_TEST(store->findCount() >= 1U);

    const std::size_t before_patch = store->updateCount();
    const auto patch = dispatch(makeRequest(
        bhttp::verb::patch, "/api/v1/links/bk31rw01", "{\"enabled\":false}"));
    BOOST_TEST(patch.result_int() == 200);
    BOOST_TEST(patch.body().find("\"status\":\"disabled\"")
               != std::string::npos);
    BOOST_TEST(store->updateCount() > before_patch);

    const std::size_t before_disable = store->updateCount();
    const auto disable = dispatch(
        makeRequest(bhttp::verb::post, "/api/v1/links/bk31rw01/disable"));
    BOOST_TEST(disable.result_int() == 200);
    BOOST_TEST(disable.body().find("\"status\":\"disabled\"")
               != std::string::npos);
    BOOST_TEST(store->updateCount() > before_disable);

    // Re-enable before deleting so the restore below reverts to active, which
    // keeps the lifecycle status assertions independent of earlier steps.
    const std::size_t before_enable = store->updateCount();
    const auto enable = dispatch(
        makeRequest(bhttp::verb::post, "/api/v1/links/bk31rw01/enable"));
    BOOST_TEST(enable.result_int() == 200);
    BOOST_TEST(enable.body().find("\"status\":\"active\"")
               != std::string::npos);
    BOOST_TEST(store->updateCount() > before_enable);

    const std::size_t before_delete = store->updateCount();
    const auto remove =
        dispatch(makeRequest(bhttp::verb::delete_, "/api/v1/links/bk31rw01"));
    BOOST_TEST(remove.result_int() == 200);
    BOOST_TEST(store->updateCount() > before_delete);

    const std::size_t before_restore = store->updateCount();
    const auto restore = dispatch(
        makeRequest(bhttp::verb::post, "/api/v1/links/bk31rw01/restore"));
    BOOST_TEST(restore.result_int() == 200);
    BOOST_TEST(restore.body().find("\"status\":\"active\"")
               != std::string::npos);
    BOOST_TEST(store->updateCount() > before_restore);

    BOOST_TEST(
        !url_shortener::linkRepository().getBySlug("bk31rw01").has_value());
}

/**
 * [Unit][Http] The legacy no-window stats response reads the link through the
 * installed store; a missing slug is 404 not_found as today (C3).
 *
 * Scenario:
 *   Given a FakeLinkStore holding a link with known redirect counters.
 *   When GET /api/v1/links/{slug}/stats is dispatched without window params.
 *   Then the 200 body carries the stored counters and an absent slug yields
 *   404 not_found.
 *
 * If this breaks, first check:
 *   - handleLinkStats' no-window branch using store().findBySlug.
 */
BOOST_FIXTURE_TEST_CASE(legacy_stats_reads_through_backend, BackendFixture)
{
    auto store = std::make_shared<FakeLinkStore>();
    Link link = makeLink("bk31stats01");
    link.stats.total_redirects = 42;
    link.stats.redirects_24h = 7;
    link.stats.redirects_7d = 9;
    store->seed(link);
    LinkBackend::install(store);

    const auto stats = dispatch(
        makeRequest(bhttp::verb::get, "/api/v1/links/bk31stats01/stats"));
    BOOST_TEST(stats.result_int() == 200);
    BOOST_TEST(stats.body().find("\"total_redirects\":42")
               != std::string::npos);
    BOOST_TEST(stats.body().find("\"redirects_24h\":7") != std::string::npos);

    const auto missing = dispatch(
        makeRequest(bhttp::verb::get, "/api/v1/links/bk31missing01/stats"));
    BOOST_TEST(missing.result_int() == 404);
    BOOST_TEST(missing.body().find("\"code\":\"not_found\"")
               != std::string::npos);
}

/**
 * [Unit][Http] The qr placeholder checks link existence through the installed
 * store: a present slug is 501 feature_not_enabled, a missing slug is 404.
 *
 * Scenario:
 *   Given a FakeLinkStore holding one link.
 *   When GET /api/v1/links/{slug}/qr is dispatched for the present and an
 *   absent slug.
 *   Then the responses are 501 feature_not_enabled and 404 not_found.
 *
 * If this breaks, first check:
 *   - handlePlaceholderFeature using store().findBySlug for the existence gate.
 */
BOOST_FIXTURE_TEST_CASE(placeholder_existence_through_backend, BackendFixture)
{
    auto store = std::make_shared<FakeLinkStore>();
    store->seed(makeLink("bk31qr01"));
    LinkBackend::install(store);

    const auto present =
        dispatch(makeRequest(bhttp::verb::get, "/api/v1/links/bk31qr01/qr"));
    BOOST_TEST(present.result_int() == 501);
    BOOST_TEST(present.body().find("\"code\":\"feature_not_enabled\"")
               != std::string::npos);

    const auto missing = dispatch(
        makeRequest(bhttp::verb::get, "/api/v1/links/bk31missing02/qr"));
    BOOST_TEST(missing.result_int() == 404);
    BOOST_TEST(missing.body().find("\"code\":\"not_found\"")
               != std::string::npos);
}

/**
 * [Unit][Core] generateUniqueSlug asks the installed store's slugExists and
 * gives up when every candidate is reported as taken.
 *
 * Scenario:
 *   Given an AlwaysExistsStore installed.
 *   When generateUniqueSlug is called with a generator returning a fixed
 *   candidate.
 *   Then it exhausts its bounded retries and returns no value.
 *
 * If this breaks, first check:
 *   - generateUniqueSlug calling app::LinkBackend::store().slugExists (not
 *     linkRepository().slugExists).
 */
BOOST_FIXTURE_TEST_CASE(generate_unique_slug_exhausts_through_backend,
                        BackendFixture)
{
    LinkBackend::install(std::make_shared<AlwaysExistsStore>());

    ServerConfig config = testConfig();
    const auto generated = url_shortener::generateUniqueSlug(
        config, [](std::uint32_t) { return std::string("bk31fixed"); });
    BOOST_TEST(!generated.has_value());
}

/**
 * [Unit][Http] After LinkBackend::reset() the legacy default path serves
 * reads again (smoke).
 *
 * Scenario:
 *   Given a link created directly in the legacy repository and no store
 *   installed (reset()).
 *   When GET /api/v1/links/{slug} is dispatched.
 *   Then the legacy LegacyLinkStore default resolves it to 200.
 *
 * If this breaks, first check:
 *   - LinkBackend::reset() restoring the LegacyLinkStore default and the
 *     handlers honoring it.
 */
BOOST_FIXTURE_TEST_CASE(legacy_path_after_reset, BackendFixture)
{
    LinkBackend::reset();

    Link link = makeLink("bk31legacy01");
    link.id = url_shortener::generateId();
    BOOST_REQUIRE(url_shortener::linkRepository().create(link));
    url_shortener::linkCache().erase(link.slug);

    const auto get =
        dispatch(makeRequest(bhttp::verb::get, "/api/v1/links/bk31legacy01"));
    BOOST_TEST(get.result_int() == 200);
    BOOST_TEST(get.body().find("\"slug\":\"bk31legacy01\"")
               != std::string::npos);
}

namespace
{

/// Direct-write patterns forbidden anywhere under src/http or src/cli
/// (threat model T-40): every create/update must flow through
/// LinkCommandService, which owns URL/slug/SSRF validation. Matched as
/// whitespace-tolerant regexes over whole files, so a reference alias
/// (`auto& s = store(); s.create(`), a pointer (`bundle.store->create(`) and
/// a call wrapped across lines are all caught. Reads (`findBySlug`,
/// `slugExists`) and the best-effort `recordRedirect` are allowed.
const std::array<std::regex, 2> k_forbidden_writes = {
    std::regex(R"((\.|->)\s*(create|update)\s*\()"),
    std::regex(R"(updateLinkAndInvalidateCache\s*\()"),
};

/// Storage headers src/http and src/cli must not include: reaching a store or
/// repository type directly is the first step to bypassing the service.
const std::array<std::regex, 1> k_forbidden_includes = {
    std::regex(R"(#\s*include\s*[<"][^>"]*)"
               R"((storage/link_repository|app/repository_link_store)"
               R"(|storage/i_metadata_repository)\.hpp[>"])"),
};

/// Removes `//` line comments and `/* */` block comments so prose that
/// mentions a forbidden call (e.g. a doc comment) is not reported.
std::string strip_comments(const std::string& text)
{
    static const std::regex block(R"(/\*[\s\S]*?\*/)");
    static const std::regex line(R"(//[^\n]*)");
    return std::regex_replace(std::regex_replace(text, block, " "), line, " ");
}

/// Scans every `*.cpp` and `*.hpp` under @p dir (recursively) and returns
/// `(path, matched text)` pairs for forbidden writes and includes.
std::vector<std::pair<std::string, std::string>> forbidden_writes_under(
    const fs::path& dir)
{
    std::vector<std::pair<std::string, std::string>> hits;
    if (!fs::exists(dir)) {
        return hits;
    }
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        const auto ext = entry.path().extension();
        if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".hpp")) {
            continue;
        }
        std::ifstream in(entry.path());
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string raw = buffer.str();
        const std::string code = strip_comments(raw);
        for (const auto& re : k_forbidden_writes) {
            for (std::sregex_iterator it(code.begin(), code.end(), re), done;
                 it != done;
                 ++it)
            {
                hits.emplace_back(entry.path().string(), it->str());
            }
        }
        for (const auto& re : k_forbidden_includes) {
            for (std::sregex_iterator it(raw.begin(), raw.end(), re), done;
                 it != done;
                 ++it)
            {
                hits.emplace_back(entry.path().string(), it->str());
            }
        }
    }
    return hits;
}

/// The project source root, or an empty path when SOURCE_DIR is not defined
/// (e.g. a direct non-CTest run).
fs::path source_root()
{
#ifdef SOURCE_DIR
    return fs::path(SOURCE_DIR);
#else
    return {};
#endif
}

}  // namespace

/**
 * [Unit][Http] Guard: no file under src/http or src/cli performs a direct
 * store/legacy write — every create/update stays upstream in
 * LinkCommandService (threat model T-40).
 *
 * Scenario:
 *   Scans every .cpp and .hpp under src/http and src/cli for any
 *   `.create(` / `->create(` / `.update(` / `->update(` call, the legacy
 *   write helper, and includes of the storage/store headers.
 *   Then no file matches.
 *
 * If this breaks, first check:
 *   - a handler/CLI path gained a direct store write; route it through
 *     LinkCommandService instead so URL/slug/SSRF validation runs first.
 */
BOOST_AUTO_TEST_CASE(no_direct_store_writes_in_http_or_cli)
{
    const fs::path root = source_root();
    if (root.empty()) {
        BOOST_TEST_MESSAGE("SOURCE_DIR not defined; skipping (run via CTest)");
        return;
    }

    std::vector<std::pair<std::string, std::string>> hits;
    for (const auto* sub : {"src/http", "src/cli"}) {
        auto found = forbidden_writes_under(root / sub);
        hits.insert(hits.end(), found.begin(), found.end());
    }

    for (const auto& [path, line] : hits) {
        BOOST_TEST(false,
                   "direct store/legacy write in " << path << ": " << line);
    }
    BOOST_TEST(hits.empty());
}

/**
 * [Unit][Http] Negative self-test: the scanner flags a planted direct write,
 * proving the guard would catch a real violation rather than passing
 * vacuously.
 *
 * Scenario:
 *   Temp files planting each bypass form - canonical call, reference alias,
 *   pointer through a bundle, a call wrapped across lines, a header, and a
 *   forbidden storage include - are each reported; a file with only reads and
 *   a write mentioned in a comment is not (security review LOW on 02.0/09).
 *
 * If this breaks, first check:
 *   - k_forbidden_writes / k_forbidden_includes or strip_comments changed.
 */
BOOST_AUTO_TEST_CASE(guard_detects_a_planted_direct_write)
{
    const fs::path dir = fs::temp_directory_path()
        / ("url_shortener_store_write_selftest_"
           + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto write = [&](const char* name, const std::string& body)
    { std::ofstream(dir / name) << body; };
    // One file per bypass form the guard must catch.
    write("canonical.cpp",
          "void f() { app::LinkBackend::store().create(l); }\n");
    write("alias.cpp",
          "void f() { auto& s = app::LinkBackend::store(); s.update(l); }\n");
    write("pointer.cpp", "void f() { bundle.store->create(l); }\n");
    write(
        "wrapped.cpp",
        "void f() {\n    app::LinkBackend::store()\n        .create(l);\n}\n");
    write("header.hpp", "inline void f() { bundle.store->update(l); }\n");
    write("include.cpp",
          "#include <url_shortener/app/repository_link_store.hpp>\n");
    // Must not be reported: reads, and a write mentioned only in a comment.
    write("clean.cpp",
          "// store().create(l) is forbidden here\n"
          "void g() { const auto l = app::LinkBackend::store().findBySlug(s);"
          " (void)app::LinkBackend::store().slugExists(s); }\n");

    const auto hits = forbidden_writes_under(dir);
    fs::remove_all(dir);

    std::set<std::string> flagged;
    for (const auto& [path, text] : hits) {
        flagged.insert(fs::path(path).filename().string());
    }
    const std::set<std::string> expected = {"canonical.cpp",
                                            "alias.cpp",
                                            "pointer.cpp",
                                            "wrapped.cpp",
                                            "header.hpp",
                                            "include.cpp"};
    BOOST_TEST(flagged == expected);
}

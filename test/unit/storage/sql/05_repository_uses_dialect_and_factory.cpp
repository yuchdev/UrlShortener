#define BOOST_TEST_MODULE SqlRepositoryUsesDialectAndFactoryTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/sql/SqlMetadataRepository.hpp"

class StubDialect final : public SqlDialect {
public:
    std::string BootstrapSchemaSql() const override { return "bootstrap"; }
    std::string InsertSql() const override { return "insert"; }
    std::string SelectByShortCodeSql() const override { return "select"; }
    std::string UpdateSql() const override { return "update"; }
    std::string DeleteSql() const override { return "delete"; }
    std::string ExistsSql() const override { return "exists"; }
    std::string ListSql() const override { return "list"; }
};

class StubSession final : public ISqlSession {
public:
    int bootstrap_calls = 0;
    int insert_calls = 0;
    bool Bootstrap(const std::string&, RepoError*) override { ++bootstrap_calls; return true; }
    bool InsertLink(const std::string&, const SqlRow&, RepoError*) override { ++insert_calls; return true; }
    std::optional<SqlRow> SelectByShortCode(const std::string&, const std::string&, RepoError* error) override { if (error) *error = RepoError::not_found; return std::nullopt; }
    bool UpdateLink(const std::string&, const SqlRow&, RepoError*) override { return true; }
    bool DeleteLink(const std::string&, const std::string&, RepoError*) override { return true; }
    bool Exists(const std::string&, const std::string&, bool* exists, RepoError*) override { *exists = false; return true; }
    std::vector<SqlRow> List(const std::string&, const std::optional<std::string>&, bool, std::size_t, std::size_t, RepoError*) override { return {}; }
};

class StubFactory final : public ISqlSessionFactory {
public:
    std::shared_ptr<StubSession> session = std::make_shared<StubSession>();
    std::unique_ptr<ISqlSession> Create(RepoError*) const override { return std::make_unique<StubSession>(*session); }
};

BOOST_AUTO_TEST_CASE(repository_invokes_bootstrap_and_insert)
{
    auto factory = std::make_shared<StubFactory>();
    auto repo = SqlMetadataRepository(factory, std::make_shared<StubDialect>(), std::make_shared<SqlMetadataRowMapper>());
    CreateLinkRequest req{"abc123", "https://example.com"};
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink(req, "id1", std::chrono::system_clock::time_point{}, &err));
    BOOST_TEST(err == RepoError::none);
}

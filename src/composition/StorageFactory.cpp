#include "url_shortener/composition/StorageFactory.hpp"

#include <stdexcept>

#include "url_shortener/storage/inmemory/InMemoryAnalyticsSink.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"
#include "url_shortener/storage/postgres/PostgresSessionFactory.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"
#include "url_shortener/storage/redis/RedisCacheStore.hpp"
#include "url_shortener/storage/redis/RedisRateLimiter.hpp"
#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"
#include "url_shortener/storage/sql/SqlMetadataRepository.hpp"
#include "url_shortener/storage/sql/SqlMetadataRowMapper.hpp"
#include "url_shortener/storage/sqlite/SqliteSessionFactory.hpp"
#include "url_shortener/storage/sqlite/SqliteSqlDialect.hpp"

namespace {
class NoopAnalyticsSink final : public IAnalyticsSink { public: bool Emit(const LinkAccessEvent&, AnalyticsError*) override { return true; } bool Flush(AnalyticsError*) override { return true; } };
class NoopCacheStore final : public ICacheStore { public: std::optional<CacheValue> Get(const std::string&, CacheError*) override { return std::nullopt; } bool Set(const std::string&, const CacheValue&, std::optional<std::chrono::seconds>, CacheError*) override { return true; } bool Delete(const std::string&, CacheError*) override { return true; } bool ClearByPrefix(const std::string&, CacheError*) override { return true; } };
}

StorageAdapters BuildStorageAdapters(const StorageConfig& config, const IClock& clock) {
    StorageAdapters out;
    if (config.metadata.backend == "inmemory") out.metadata = std::make_shared<InMemoryMetadataRepository>();
    else {
        SqlConnectionConfig sql; std::shared_ptr<ISqlSessionFactory> sf; std::shared_ptr<SqlDialect> dialect;
        if (config.metadata.backend == "sqlite") { sql.backend = SqlBackendKind::sqlite; sql.dsn = *config.metadata.sqlite_path; sf = std::make_shared<SqliteSessionFactory>(sql); dialect = std::make_shared<SqliteSqlDialect>(); }
        else { sql.backend = SqlBackendKind::postgres; sql.dsn = *config.metadata.postgres_dsn; sf = std::make_shared<PostgresSessionFactory>(sql); dialect = std::make_shared<PostgresSqlDialect>(); }
        out.metadata = std::make_shared<SqlMetadataRepository>(sf, dialect, std::make_shared<SqlMetadataRowMapper>());
    }
    if (config.cache.backend == "none") out.cache = std::make_shared<NoopCacheStore>();
    else if (config.cache.backend == "inmemory") out.cache = std::make_shared<InMemoryCacheStore>(clock);
    else { RedisCacheConfig c; const auto p = config.cache.redis_address->find(':'); c.host = config.cache.redis_address->substr(0,p); c.port = std::stoi(config.cache.redis_address->substr(p+1)); if(config.cache.default_ttl_seconds) c.default_ttl=std::chrono::seconds(*config.cache.default_ttl_seconds); out.cache = std::make_shared<RedisCacheStore>(c);}    
    out.analytics = config.analytics.backend == "inmemory" ? std::make_shared<InMemoryAnalyticsSink>() : std::make_shared<NoopAnalyticsSink>();
    if (!config.rate_limit.enabled) out.rate_limiter = nullptr;
    else if (config.rate_limit.backend == "inmemory") out.rate_limiter = std::make_shared<InMemoryRateLimiter>();
    else { RedisRateLimiterConfig c; const auto p = config.rate_limit.redis_address->find(':'); c.host = config.rate_limit.redis_address->substr(0,p); c.port = std::stoi(config.rate_limit.redis_address->substr(p+1)); out.rate_limiter = std::make_shared<RedisRateLimiter>(c);}    
    return out;
}

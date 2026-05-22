/** @file 06_slug_collision_retry_test.cpp @brief Unit test: generated slug collision retries are bounded. */
#define BOOST_TEST_MODULE SlugCollisionRetryTest
#include <boost/test/unit_test.hpp>
#include <chrono>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Core] Generated slug collision retries are bounded.
 *
 * Scenario:
 *   Given the repository already contains a generated candidate slug.
 *   When generator repeatedly returns the same colliding slug.
 *   Then generateUniqueSlug() gives up and returns no value.
 *
 * API/Feature covered:
 *   - Generated slug collision handling and retry limits.
 *
 * If this breaks:
 *   - Create flow may loop excessively or allow duplicate slugs.
 *   - First check retry cap and collision existence checks.
 */
BOOST_AUTO_TEST_CASE(generated_slug_collision_retry_behavior)
{
    const std::string collision_slug = "col" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    Link existing;
    existing.id = generateId();
    existing.slug = collision_slug;
    existing.target_url = "https://example.com";
    existing.created_at = currentTimestamp();
    existing.updated_at = existing.created_at;
    BOOST_REQUIRE(linkRepository().create(existing));

    ServerConfig cfg;
    cfg.shortener_generated_slug_length = static_cast<uint32_t>(collision_slug.size());
    const auto generated = generateUniqueSlug(cfg, [&](uint32_t) { return collision_slug; });
    BOOST_TEST(!generated.has_value());
}

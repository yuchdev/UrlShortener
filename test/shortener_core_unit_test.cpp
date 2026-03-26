/**
 * @file shortener_core_unit_test.cpp
 * @brief Unit tests for Stage 1 shortener validation and redirect helpers.
 */
#define BOOST_TEST_MODULE ShortenerCoreUnitTest
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <optional>
#include <string>

// Intentionally include implementation to unit-test anonymous-namespace helpers.
#include "../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(url_validation_accepts_http_https_absolute)
{
    ServerConfig cfg;
    cfg.shortener_allow_private_targets = true;
    BOOST_TEST(normalizeTargetUrl("http://example.com/path", cfg).has_value());
    BOOST_TEST(normalizeTargetUrl("https://example.com/path", cfg).has_value());
}

BOOST_AUTO_TEST_CASE(url_validation_rejects_invalid_or_unsafe)
{
    ServerConfig cfg;
    cfg.shortener_allow_private_targets = false;
    BOOST_TEST(!normalizeTargetUrl("ftp://example.com", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("javascript:alert(1)", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("/relative", cfg).has_value());
    BOOST_TEST(!normalizeTargetUrl("https://user:pass@example.com", cfg).has_value());
}

BOOST_AUTO_TEST_CASE(url_normalization_lowercases_and_strips_default_ports)
{
    ServerConfig cfg;
    cfg.shortener_allow_private_targets = true;
    const auto normalized = normalizeTargetUrl("  HTTPS://Example.COM:443/Path?q=1#frag ", cfg);
    BOOST_REQUIRE(normalized.has_value());
    BOOST_TEST(*normalized == "https://example.com/Path?q=1#frag");
}

BOOST_AUTO_TEST_CASE(slug_validator_accepts_boundary_values)
{
    BOOST_TEST(isValidSlug("Ab1"));
    BOOST_TEST(isValidSlug("a" + std::string(63, 'b')));
}

BOOST_AUTO_TEST_CASE(slug_validator_rejects_invalid_values)
{
    BOOST_TEST(!isValidSlug("ab"));
    BOOST_TEST(!isValidSlug("_abc"));
    BOOST_TEST(!isValidSlug("ab!c"));
    BOOST_TEST(!isValidSlug(std::string(65, 'a')));
}

BOOST_AUTO_TEST_CASE(generated_slug_collision_retry_is_bounded)
{
    const std::string collision_slug = "zzz" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    Link link;
    link.id = generateId();
    link.slug = collision_slug;
    link.target_url = "https://example.com";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    BOOST_REQUIRE(linkRepository().create(link));

    ServerConfig cfg;
    cfg.shortener_generated_slug_length = static_cast<uint32_t>(collision_slug.size());

    const auto generated = generateUniqueSlug(cfg, [&](uint32_t) { return collision_slug; });
    BOOST_TEST(!generated.has_value());
}

BOOST_AUTO_TEST_CASE(expiry_evaluation_logic)
{
    BOOST_TEST(!isExpired(std::nullopt));
    BOOST_TEST(isExpired(std::optional<std::string>{"2000-01-01T00:00:00Z"}));
}

BOOST_AUTO_TEST_CASE(redirect_status_selection_from_redirect_type)
{
    Link temp;
    temp.redirect_type = RedirectType::temporary;
    BOOST_TEST(redirectStatusFor(temp) == http::status::found);

    Link perm;
    perm.redirect_type = RedirectType::permanent;
    BOOST_TEST(redirectStatusFor(perm) == http::status::moved_permanently);
}

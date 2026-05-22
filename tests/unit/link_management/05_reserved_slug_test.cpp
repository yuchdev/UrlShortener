/** @file 05_reserved_slug_test.cpp @brief Unit test: reserved slug denylist is matched case-insensitively. */
#define BOOST_TEST_MODULE LinkManagementReservedSlugTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Link Management] Reserved slug matching is case-insensitive.
 *
 * Scenario:
 *   Given mixed-case reserved slugs and one normal slug.
 *   When isReservedSlug() evaluates each.
 *   Then reserved names match regardless of case.
 *
 * API/Feature covered:
 *   - Stage 2 reserved slug denylist guard.
 *
 * If this breaks:
 *   - System paths may collide with user-created slugs.
 *   - First check lowercase normalization and set membership lookup.
 */
BOOST_AUTO_TEST_CASE(reserved_slug_case_insensitive_matching)
{
    BOOST_TEST(isReservedSlug("API"));
    BOOST_TEST(isReservedSlug("Stats"));
    BOOST_TEST(isReservedSlug("LoGiN"));
    BOOST_TEST(!isReservedSlug("promo-2026"));
}

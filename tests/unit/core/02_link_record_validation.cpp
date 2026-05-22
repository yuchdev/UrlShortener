#define BOOST_TEST_MODULE CoreLinkRecordValidationTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/models/CreateLinkRequest.hpp"
#include "url_shortener/storage/models/LinkRecord.hpp"

BOOST_AUTO_TEST_CASE(link_record_stores_required_fields)
{
    LinkRecord r;
    r.id = "id1";
    r.short_code = "abc123";
    r.target_url = "https://example.com";
    BOOST_TEST(r.id == "id1");
    BOOST_TEST(r.owner.has_value() == false);
}

BOOST_AUTO_TEST_CASE(create_request_validation_edges)
{
    CreateLinkRequest ok{"abc_123", "https://example.com"};
    BOOST_TEST(ok.isValid());

    CreateLinkRequest bad_code{"a", "https://example.com"};
    BOOST_TEST(!bad_code.isValid());

    CreateLinkRequest bad_target{"abc123", ""};
    BOOST_TEST(!bad_target.isValid());
}

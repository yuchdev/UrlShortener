#define BOOST_TEST_MODULE InMemoryMetadataGetUnknownTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(get_unknown_returns_empty_optional)
{
    InMemoryMetadataRepository repo;
    auto got = repo.GetByShortCode("missing");
    BOOST_TEST(!got.has_value());
}

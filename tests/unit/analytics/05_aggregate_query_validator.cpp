#define BOOST_TEST_MODULE AggregateQueryValidator
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/AggregateQuery.hpp"
using namespace url_shortener::analytics;
BOOST_AUTO_TEST_CASE(valid_range_accepted){AggregateQuery q; q.from={}; q.to=q.from+std::chrono::hours(24); BOOST_TEST(AggregateQueryValidator::Validate(&q)); BOOST_TEST(q.bucket.value()==AggregateBucket::day);} 
BOOST_AUTO_TEST_CASE(from_gt_to_rejected){AggregateQuery q; q.from=std::chrono::system_clock::now(); q.to=q.from-std::chrono::hours(1); BOOST_TEST(!AggregateQueryValidator::Validate(&q));}
BOOST_AUTO_TEST_CASE(excessive_rejected){AggregateQuery q; q.from={}; q.to=q.from+std::chrono::hours(24*400); BOOST_TEST(!AggregateQueryValidator::Validate(&q));}

BOOST_AUTO_TEST_CASE(unsupported_bucket_rejected){AggregateQuery q; q.from={}; q.to=q.from+std::chrono::hours(1); q.bucket=static_cast<AggregateBucket>(999); BOOST_TEST(!AggregateQueryValidator::Validate(&q));}

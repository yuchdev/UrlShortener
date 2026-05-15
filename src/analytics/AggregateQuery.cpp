#include "url_shortener/analytics/AggregateQuery.hpp"

namespace url_shortener::analytics {

bool AggregateQueryValidator::Validate(AggregateQuery* query, AnalyticsError* error)
{
    if (!query) return false;
    if (!query->bucket.has_value()) query->bucket = AggregateBucket::day;
    if (query->bucket.has_value()) {
        switch (*query->bucket) {
        case AggregateBucket::hour:
        case AggregateBucket::day:
        case AggregateBucket::week:
            break;
        default:
            if (error) { error->code = AnalyticsErrorCode::validation; error->message = "unsupported bucket"; }
            return false;
        }
    }

    if (query->from > query->to) {
        if (error) { error->code = AnalyticsErrorCode::validation; error->message = "from > to"; }
        return false;
    }
    const auto days = std::chrono::duration_cast<std::chrono::hours>(query->to - query->from).count() / 24;
    if (days > 366) {
        if (error) { error->code = AnalyticsErrorCode::validation; error->message = "range too large"; }
        return false;
    }
    return true;
}

} // namespace

#pragma once
#include <string>

#include "url_shortener/analytics/i_click_event_repository.hpp"

namespace url_shortener::http
{

/**
 * @brief HTTP handler helper that queries `IClickEventRepository` for
 * aggregate click statistics and serializes the result to JSON.
 *
 * Encapsulates the query/serialization logic so the same behavior can be
 * shared across multiple endpoint entry points without duplicating the
 * parameter-parsing and JSON-rendering code.
 *
 * Lifetime: does not own `repo`; the caller must keep it alive for at
 * least as long as this object.
 */
class AnalyticsStatsHandler
{
public:
    /**
     * @brief Stores a reference to `repo`; does not start any background
     * work.
     * @param repo Click-event repository queried by `Handle`; must
     *        outlive this object.
     */
    explicit AnalyticsStatsHandler(analytics::IClickEventRepository& repo)
        : repo_(repo)
    {
    }

    /**
     * @brief Validates the query parameters, queries the repository for
     * aggregate statistics, and writes a JSON response body and HTTP
     * status code to the out-params.
     *
     * Both `body` and `status_code` are always written; the function
     * currently always returns `true`.
     * @param slug       Short-link slug to query; a 404 response is
     *        produced for an empty slug.
     * @param from       Inclusive query start time as a Unix epoch-second
     *        integer string.
     * @param to         Exclusive query end time as a Unix epoch-second
     *        integer string; must be strictly greater than `from`.
     * @param bucket     Aggregation granularity: `"hour"`, `"day"`, or
     *        `"week"`; a 400 response is produced for any other value.
     * @param body       Receives the JSON response body; must not be
     *        null.  Always written before returning.
     * @param status_code Receives the HTTP status code (200, 400, 404,
     *        or 500); must not be null.  Always written before returning.
     * @param request_id Optional request correlation ID embedded in
     *        error response bodies for tracing.
     * @return Always `true` in the current implementation.
     */
    bool Handle(const std::string& slug,
                const std::string& from,
                const std::string& to,
                const std::string& bucket,
                std::string* body,
                int* status_code,
                std::string request_id = "") const;

private:
    ///< Click-event repository used for aggregate queries; not owned,
    ///< must outlive this object.
    analytics::IClickEventRepository& repo_;
};

}  // namespace url_shortener::http

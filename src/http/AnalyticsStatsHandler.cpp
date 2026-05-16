#include "url_shortener/http/AnalyticsStatsHandler.hpp"

#include <chrono>
#include <sstream>

namespace url_shortener::http {
namespace {
bool parse_epoch(const std::string& s, std::chrono::system_clock::time_point* out) {
    try { *out = std::chrono::system_clock::time_point{std::chrono::seconds(std::stoll(s))}; return true; } catch (...) { return false; }
}
}

bool AnalyticsStatsHandler::Handle(const std::string& slug, const std::string& from, const std::string& to, const std::string& bucket,
                                   std::string* body, int* status_code, std::string request_id) const {
    if (slug.empty()) { *status_code = 404; *body = "{\"error\":\"not_found\",\"request_id\":\""+request_id+"\"}"; return true; }
    analytics::AggregateQuery q; q.slug = slug;
    if (!parse_epoch(from, &q.from) || !parse_epoch(to, &q.to) || q.from >= q.to) {
        *status_code = 400; *body = "{\"error\":\"invalid_timestamp\",\"request_id\":\""+request_id+"\"}"; return true;
    }
    if (bucket == "hour") q.bucket = analytics::AggregateBucket::hour;
    else if (bucket == "day") q.bucket = analytics::AggregateBucket::day;
    else if (bucket == "week") q.bucket = analytics::AggregateBucket::week;
    else { *status_code = 400; *body = "{\"error\":\"invalid_bucket\",\"request_id\":\""+request_id+"\"}"; return true; }
    analytics::AggregateStats stats;
    if (!repo_.GetAggregateStats(q, &stats, nullptr)) { *status_code = 500; *body = "{\"error\":\"repository\",\"request_id\":\""+request_id+"\"}"; return true; }
    std::ostringstream os;
    os << "{\"slug\":\""<<slug<<"\",\"total_attempts\":"<<stats.total_attempts<<",\"successful_redirects\":"<<stats.successful_redirects
       <<",\"attempts_by_status_code\":{";
    for (size_t i=0;i<stats.status_code_counts.size();++i){ if(i) os<<","; os<<"\""<<stats.status_code_counts[i].status_code<<"\":"<<stats.status_code_counts[i].count; }
    os << "},\"attempts_by_domain\":{";
    for (size_t i=0;i<stats.domain_counts.size();++i){ if(i) os<<","; os<<"\""<<stats.domain_counts[i].domain<<"\":"<<stats.domain_counts[i].count; }
    os << "},\"time_buckets\":[";
    for (size_t i=0;i<stats.time_buckets.size();++i){ if(i) os<<","; os<<"{\"count\":"<<stats.time_buckets[i].count<<"}"; }
    os << "],\"from\":"<<from<<",\"to\":"<<to<<",\"bucket\":\""<<bucket<<"\"}";
    *status_code = 200; *body = os.str(); return true;
}

}

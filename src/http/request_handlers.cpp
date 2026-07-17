#include <url_shortener/http/request_handlers.h>
#include <url_shortener/http/RouteRegistry.hpp>
#include <url_shortener/http/RouterBuilder.hpp>
#include <url_shortener/core/config.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/storage/link_repository.h>
#include <url_shortener/analytics/click_event_queue.h>
#include <url_shortener/uri_map_singleton.h>
#include <url_shortener/observability/LoggerFactory.h>
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
#include "url_shortener/http/AnalyticsStatsHandler.hpp"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <algorithm>
#include <functional>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <mutex>
#include <map>

namespace beast = boost::beast;
namespace bhttp = beast::http;

namespace url_shortener {

static constexpr std::string_view request_id_header = "X-Request-Id";
static constexpr std::string_view internal_request_id_header = "X-Internal-Request-Id";

static const http::Router& applicationRouter()
{
    static const auto router = http::RouterBuilder::buildApplicationRouter();
    return router;
}

storage::memory::InMemoryClickEventRepository& analyticsClickRepository() {
    static storage::memory::InMemoryClickEventRepository instance;
    return instance;
}

static std::string getQueryParam(const std::string& query_string, const std::string& key) {
    const std::string prefix = key + "=";
    const auto pos = query_string.find(prefix);
    if (pos == std::string::npos) return {};
    const auto val_start = pos + prefix.size();
    const auto val_end = query_string.find('&', val_start);
    return val_end == std::string::npos
        ? query_string.substr(val_start)
        : query_string.substr(val_start, val_end - val_start);
}


static std::string sanitizeLogValue(std::string value)
{
    constexpr size_t max_len = 160;
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
        return c < 0x20 && c != ' ';
    }), value.end());
    if (value.size() > max_len) {
        value = value.substr(0, max_len);
    }
    return value;
}

static bool isValidRequestId(const std::string& value, const uint32_t max_len)
{
    if (value.empty() || value.size() > max_len) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || c == '.';
    });
}

static std::string generateRequestId()
{
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    const uint64_t hi = dist(rng);
    const uint64_t lo = dist(rng);
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hi
        << std::setw(16) << std::setfill('0') << lo;
    return out.str();
}

static std::string statusClass(const unsigned status)
{
    return std::to_string(status / 100) + "xx";
}

static std::string makeShortUrl(
    const bhttp::request<bhttp::string_body>&,
    const ServerConfig& config,
    const bool,
    const std::string& slug)
{
    std::string base = config.shortener_base_domain;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + "/" + slug;
}

static std::string serializeLink(const Link& link, const std::string& short_url)
{
    std::ostringstream body;
    body << "{\"id\":" << url_shortener::jsonString(link.id)
         << ",\"slug\":" << url_shortener::jsonString(link.slug)
         << ",\"url\":" << url_shortener::jsonString(link.target_url)
         << ",\"short_url\":" << url_shortener::jsonString(short_url)
         << ",\"created_at\":" << url_shortener::jsonString(link.created_at)
         << ",\"updated_at\":" << url_shortener::jsonString(link.updated_at)
         << ",\"status\":" << url_shortener::jsonString(url_shortener::linkStatusToString(url_shortener::resolveLinkStatus(link)))
         << ",\"redirect_type\":" << url_shortener::jsonString(url_shortener::redirectTypeToString(link.redirect_type))
         << ",\"tags\":[";
    for (size_t i = 0; i < link.tags.size(); ++i) {
        if (i > 0) {
            body << ',';
        }
        body << url_shortener::jsonString(link.tags[i]);
    }
    body << "],\"metadata\":{";
    if (!link.metadata.empty()) {
        std::vector<std::pair<std::string, std::string>> metadata_items(link.metadata.begin(), link.metadata.end());
        std::sort(metadata_items.begin(), metadata_items.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
        for (size_t i = 0; i < metadata_items.size(); ++i) {
            if (i > 0) {
                body << ',';
            }
            body << url_shortener::jsonString(metadata_items[i].first)
                 << ':'
                 << url_shortener::jsonString(metadata_items[i].second);
        }
    }
    body << "},\"campaign\":";
    if (link.campaign.has_value()) {
        body << '{';
        bool first = true;
        const auto appendCampaignField = [&](const char* key, const std::optional<std::string>& value) {
            if (!value.has_value()) {
                return;
            }
            if (!first) {
                body << ',';
            }
            first = false;
            body << url_shortener::jsonString(key) << ':' << url_shortener::jsonString(*value);
        };
        appendCampaignField("name", link.campaign->name);
        appendCampaignField("source", link.campaign->source);
        appendCampaignField("medium", link.campaign->medium);
        appendCampaignField("term", link.campaign->term);
        appendCampaignField("content", link.campaign->content);
        appendCampaignField("id", link.campaign->id);
        body << '}';
    }
    else {
        body << "null";
    }
    body << ",\"stats\":{"
         << "\"total_redirects\":" << link.stats.total_redirects
         << ",\"redirects_24h\":" << link.stats.redirects_24h
         << ",\"redirects_7d\":" << link.stats.redirects_7d
         << ",\"last_accessed_at\":";
    if (link.stats.last_accessed_at.has_value()) {
        body << url_shortener::jsonString(*link.stats.last_accessed_at);
    }
    else {
        body << "null";
    }
    body << "}"
         << "}";
    return body.str();
}

static std::string stripPort(std::string host)
{
    const auto pos = host.find(':');
    if (pos != std::string::npos) {
        host = host.substr(0, pos);
    }
    return host;
}

static std::optional<Link::Campaign> extractCampaign(const std::string& body)
{
    const auto raw = url_shortener::extractJsonValueToken(body, "campaign");
    if (!raw.has_value()) {
        return std::nullopt;
    }
    if (*raw == "null") {
        return Link::Campaign{};
    }
    if (raw->size() < 2 || raw->front() != '{' || raw->back() != '}') {
        return std::nullopt;
    }

    Link::Campaign campaign;
    if (const auto value = url_shortener::extractJsonStringField(*raw, "name"); value.has_value()) {
        campaign.name = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "source"); value.has_value()) {
        campaign.source = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "medium"); value.has_value()) {
        campaign.medium = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "term"); value.has_value()) {
        campaign.term = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "content"); value.has_value()) {
        campaign.content = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "id"); value.has_value()) {
        campaign.id = *value;
    }
    return campaign;
}


std::optional<std::string> generateUniqueSlug(
    const ServerConfig& config,
    const std::function<std::string(uint32_t)>& generator)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        const auto candidate = generator(config.shortener_generated_slug_length);
        if (!url_shortener::isReservedSlug(candidate) && !linkRepository().slugExists(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string resolveRequestId(const bhttp::request<bhttp::string_body>& req, const ServerConfig& config)
{
    const std::string incoming = std::string(req[request_id_header]);
    if (isValidRequestId(incoming, config.request_id_max_length)) {
        return incoming;
    }

    static std::atomic<uint64_t> invalid_count{0};
    if (!incoming.empty() && invalid_count.fetch_add(1) % 100 == 0) {
        logStructured("WARN", "invalid inbound request id ignored", { {"component", "url_shortener"} });
    }
    return generateRequestId();
}

std::string requestIdFromRequest(const bhttp::request<bhttp::string_body>& req)
{
    const std::string internal = std::string(req[internal_request_id_header]);
    if (!internal.empty()) {
        return internal;
    }
    return generateRequestId();
}

std::string routeLabelFor(const std::string& target)
{
    // Delegate to the explicit route registry so label classification has a
    // single source of truth. Dispatch behavior is unchanged in this slice.
    return http::routeLabelForTarget(target);
}

void recordHttpMetric(const bhttp::request<bhttp::string_body>& req, const std::string& route_label, const unsigned status)
{
    auto& metrics = metricsRegistry();
    const std::string key = "method=" + std::string(req.method_string()) + ",route=" + route_label + ",status_class=" + statusClass(status);
    std::lock_guard lock(metrics.mutex);
    metrics.http_requests_total[key] += 1;
}

void logStructured(const std::string& level, const std::string& msg, const std::map<std::string, std::string>& fields)
{
    std::ostringstream out;
    out << "ts=" << url_shortener::currentTimestamp() << " level=" << level << " msg=" << url_shortener::jsonString(msg);
    for (const auto& [key, value] : fields) {
        out << ' ' << key << '=' << url_shortener::jsonString(sanitizeLogValue(value));
    }
    auto logger = url_shortener::observability::LoggerFactory::Component("http_server");
    logger.info(msg, {});
}

void configureTlsVersion(boost::asio::ssl::context& context, const std::string& min_version)
{
    auto* native = context.native_handle();
    SSL_CTX_set_min_proto_version(native, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(native, TLS1_3_VERSION);

    if (min_version == "TLS1.3") {
        SSL_CTX_set_min_proto_version(native, TLS1_3_VERSION);
    }
}

uint64_t certExpiryDaysRemaining(const std::string& cert_path)
{
    FILE* file = fopen(cert_path.c_str(), "r");
    if (file == nullptr) {
        throw std::runtime_error("Cannot open certificate file: " + cert_path);
    }

    X509* cert = PEM_read_X509(file, nullptr, nullptr, nullptr);
    fclose(file);
    if (cert == nullptr) {
        throw std::runtime_error("Cannot parse certificate file: " + cert_path);
    }

    const ASN1_TIME* not_after = X509_get0_notAfter(cert);
    int days = 0;
    int seconds = 0;
    if (ASN1_TIME_diff(&days, &seconds, nullptr, not_after) == 0) {
        X509_free(cert);
        throw std::runtime_error("Cannot calculate certificate expiry for: " + cert_path);
    }

    X509_free(cert);
    return static_cast<uint64_t>(std::max(days, 0));
}

void validatePrivateKeyFilePermissions(const std::string& key_file)
{
#if defined(__unix__) || defined(__APPLE__)
    const auto perms = std::filesystem::status(key_file).permissions();
    const bool group_read = (perms & std::filesystem::perms::group_read) != std::filesystem::perms::none;
    const bool others_read = (perms & std::filesystem::perms::others_read) != std::filesystem::perms::none;
    if (group_read || others_read) {
        throw std::runtime_error("Private key file is too permissive (group/others readable): " + key_file);
    }
#endif
}

MetricsRegistry& metricsRegistry()
{
    static MetricsRegistry registry;
    return registry;
}

std::string renderMetrics()
{
    auto& metrics = metricsRegistry();
    std::ostringstream out;
    out << "http_inflight_requests " << metrics.inflight_requests.load() << '\n';
    out << "http_parse_errors_total " << metrics.parse_errors_total.load() << '\n';
    out << "http_malformed_requests_total " << metrics.malformed_requests_total.load() << '\n';
    out << "tls_reload_success_total " << metrics.tls_reload_success_total.load() << '\n';
    out << "tls_reload_failure_total " << metrics.tls_reload_failure_total.load() << '\n';
    std::lock_guard lock(metrics.mutex);
    for (const auto& [labels, value] : metrics.http_requests_total) {
        out << "http_requests_total{" << labels << "} " << value << '\n';
    }
    return out.str();
}

bhttp::response<bhttp::string_body> makeResponse(
    const bhttp::request<bhttp::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned status,
    const std::string& body,
    const std::string& content_type)
{
    bhttp::response<bhttp::string_body> res{static_cast<bhttp::status>(status), req.version()};
    res.set(bhttp::field::server, "simple-http");
    res.keep_alive(false);
    if (!content_type.empty()) {
        res.set(bhttp::field::content_type, content_type);
    }

    if (is_tls && config.hsts_max_age.has_value()) {
        res.set(bhttp::field::strict_transport_security,
            "max-age=" + std::to_string(*config.hsts_max_age) + "; includeSubDomains");
    }

    if (is_tls) {
        res.set("X-Content-Type-Options", "nosniff");
    }

    const auto req_id = requestIdFromRequest(req);
    res.set(request_id_header, req_id);

    res.body() = body;
    res.prepare_payload();
    return res;
}

bhttp::response<bhttp::string_body> makeApiErrorResponse(
    const bhttp::request<bhttp::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned status,
    const std::string& code,
    const std::string& message)
{
    const auto req_id = requestIdFromRequest(req);
    std::ostringstream body;
    body << "{\"error\":{\"code\":" << url_shortener::jsonString(code)
         << ",\"message\":" << url_shortener::jsonString(message)
         << ",\"request_id\":" << url_shortener::jsonString(req_id)
         << "}}";
    return makeResponse(req, config, is_tls, status, body.str(), "application/json");
}

bhttp::response<bhttp::string_body> makeRedirectResponse(
    const bhttp::request<bhttp::string_body>& req,
    const ServerConfig& config)
{
    std::string host = req[bhttp::field::host].empty() ? "localhost" : std::string(req[bhttp::field::host]);
    host = stripPort(host);

    std::ostringstream location;
    location << "https://" << host;
    if (config.tls.port != 443) {
        location << ':' << config.tls.port;
    }
    location << req.target();

    bhttp::response<bhttp::string_body> res{bhttp::status::permanent_redirect, req.version()};
    res.set(bhttp::field::location, location.str());
    res.set(bhttp::field::content_type, "text/plain");
    res.set(request_id_header, requestIdFromRequest(req));
    res.body() = "Use HTTPS\n";
    res.prepare_payload();
    return res;
}

bhttp::response<bhttp::string_body> handleApplicationRequest(
    const bhttp::request<bhttp::string_body>& req,
    const ServerConfig& config,
    const bool is_tls)
{
    const auto uri = std::string(req.target());

    if (req.method() == bhttp::verb::get) {
        auto data = UriMapSingleton::getInstance().getData(uri);
        if (data.has_value()) {
            return makeResponse(req, config, is_tls, 200, data->first + "\n", data->second);
        }
        return makeResponse(req, config, is_tls, 404, "URI not found\n", "text/plain");
    }

    if (req.method() == bhttp::verb::post) {
        const auto content_type = req[bhttp::field::content_type].empty()
            ? "application/octet-stream"
            : std::string(req[bhttp::field::content_type]);
        UriMapSingleton::getInstance().saveData(uri, req.body(), content_type);
        return makeResponse(req, config, is_tls, 200, "URI saved\n", "text/plain");
    }

    if (req.method() == bhttp::verb::delete_) {
        if (UriMapSingleton::getInstance().deleteData(uri)) {
            return makeResponse(req, config, is_tls, 200, "Data deleted\n", "text/plain");
        }
        return makeResponse(req, config, is_tls, 404, "URI not found\n", "text/plain");
    }

    return makeResponse(req, config, is_tls, 400, "Invalid method\n", "text/plain");
}

bhttp::response<bhttp::string_body> handleShortenerRequest(
    const bhttp::request<bhttp::string_body>& req,
    const ServerConfig& config,
    const bool is_tls)
{
    return applicationRouter().dispatch(req, config, is_tls);
}

} // namespace url_shortener

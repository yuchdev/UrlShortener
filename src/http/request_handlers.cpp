#include <url_shortener/http/request_handlers.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/core/config.h>
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

static constexpr std::string_view shortener_api_prefix = "/api/v1/links";
static constexpr std::string_view shortener_api_compat_prefix = "/api/v1/short-urls";
static constexpr std::string_view short_redirect_prefix = "/r/";
static constexpr std::string_view request_id_header = "X-Request-Id";
static constexpr std::string_view internal_request_id_header = "X-Internal-Request-Id";

static storage::memory::InMemoryClickEventRepository& analyticsClickRepository() {
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

static void emitClickEvent(
    const bhttp::request<bhttp::string_body>& req,
    const ServerConfig& config,
    const std::string& slug,
    const std::optional<Link>& link,
    const uint16_t status_code)
{
    if (!config.analytics_enabled) {
        return;
    }

    ClickEvent event;
    event.event_id = url_shortener::generateId();
    event.occurred_at = url_shortener::currentTimestamp();
    event.slug = slug;
    if (link.has_value()) {
        event.link_id = link->id;
    }

    std::string host = req.base().count(bhttp::field::host) ? std::string(req.base().at(bhttp::field::host)) : "";
    if (const auto pos = host.find(':'); pos != std::string::npos) {
        host = host.substr(0, pos);
    }
    event.domain = url_shortener::clampString(host, 255);
    event.status_code = status_code;
    event.referrer = url_shortener::headerString(req, bhttp::field::referer, 512);
    event.user_agent = url_shortener::headerString(req, bhttp::field::user_agent, 512);

    const std::string forwarded_for = req.base().count("X-Forwarded-For") ? std::string(req.base().at("X-Forwarded-For")) : "";
    const std::string client_source = forwarded_for.empty() ? "unknown" : url_shortener::clampString(forwarded_for, 128);
    event.client_id_hash = url_shortener::hashClientIdentifier(client_source, config.analytics_client_hash_salt);

    analytics::ClickEvent analytics_event;
    analytics_event.event_id = event.event_id;
    analytics_event.occurred_at = std::chrono::system_clock::now();
    analytics_event.slug = slug;
    analytics_event.link_id = link.has_value() ? link->id : std::string{};
    analytics_event.domain = event.domain;
    analytics_event.status_code = status_code;
    analytics_event.referrer = event.referrer.value_or(std::string{});
    analytics_event.user_agent = event.user_agent.value_or(std::string{});
    analytics_event.client_id_hash = event.client_id_hash;
    analyticsClickRepository().InsertBatch({std::move(analytics_event)}, nullptr);

    (void)analyticsQueue(config).tryEnqueue(std::move(event));
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
    if (target == "/metrics") {
        return "metrics";
    }
    if (target == "/healthz") {
        return "healthz";
    }
    if (target == "/readyz") {
        return "readyz";
    }
    if (target.rfind(std::string(short_redirect_prefix), 0) == 0) {
        return "redirect_prefixed";
    }
    if (target.rfind(std::string(shortener_api_prefix), 0) == 0 || target.rfind(std::string(shortener_api_compat_prefix), 0) == 0) {
        return "api_links";
    }
    if (target.size() > 1 && target[0] == '/') {
        return "redirect_root";
    }
    return "app";
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
    const auto target = std::string(req.target());

    if (target == "/healthz" || target == "/readyz") {
        if (req.method() != bhttp::verb::get) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
        }
        return makeResponse(req, config, is_tls, 200, "ok\n", "text/plain");
    }

    if (target == "/metrics") {
        if (req.method() != bhttp::verb::get) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
        }
        return makeResponse(req, config, is_tls, 200, renderMetrics(), "text/plain");
    }

    if (target.rfind(std::string(shortener_api_prefix) + "/id/", 0) == 0) {
        if (req.method() != bhttp::verb::get) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
        }
        const std::string id = target.substr(std::string(shortener_api_prefix).size() + 4);
        const auto link = linkRepository().getById(id);
        if (!link.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
        }
        return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
    }

    if (target.rfind(std::string(shortener_api_prefix) + '/', 0) == 0) {
        const std::string resource = target.substr(std::string(shortener_api_prefix).size() + 1);
        const auto qs_pos = resource.find('?');
        const std::string resource_path = qs_pos == std::string::npos ? resource : resource.substr(0, qs_pos);
        const std::string query_string = qs_pos == std::string::npos ? std::string{} : resource.substr(qs_pos + 1);
        const auto action_pos = resource_path.find('/');
        const std::string slug = action_pos == std::string::npos ? resource_path : resource_path.substr(0, action_pos);
        const std::string action = action_pos == std::string::npos ? "" : resource_path.substr(action_pos + 1);

        if (slug.empty()) {
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
        }

        if (action == "preview") {
            if (req.method() != bhttp::verb::get) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
            }
            const auto link = url_shortener::getLinkForRead(slug);
            if (!link.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
            }
            const auto status = url_shortener::resolveLinkStatus(*link);
            std::ostringstream body;
            body << "{\"slug\":" << url_shortener::jsonString(link->slug)
                 << ",\"url\":" << url_shortener::jsonString(link->target_url)
                 << ",\"status\":" << url_shortener::jsonString(url_shortener::linkStatusToString(status))
                 << ",\"redirect_type\":" << url_shortener::jsonString(url_shortener::redirectTypeToString(link->redirect_type))
                 << ",\"enabled\":" << (link->enabled ? "true" : "false")
                 << ",\"expires_at\":";
            if (link->expires_at.has_value()) {
                body << url_shortener::jsonString(*link->expires_at);
            }
            else {
                body << "null";
            }
            body << ",\"deleted_at\":";
            if (link->deleted_at.has_value()) {
                body << url_shortener::jsonString(*link->deleted_at);
            }
            else {
                body << "null";
            }
            body << '}';
            return makeResponse(req, config, is_tls, 200, body.str(), "application/json");
        }

        if (action == "qr" || action == "routing") {
            if (req.method() != bhttp::verb::get) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
            }
            const auto link = url_shortener::getLinkForRead(slug);
            if (!link.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
            }
            return makeApiErrorResponse(
                req,
                config,
                is_tls,
                501,
                "feature_not_enabled",
                "Feature placeholder only; implementation deferred");
        }

        if (action == "stats") {
            if (req.method() != bhttp::verb::get) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
            }
            const std::string from_param = getQueryParam(query_string, "from");
            const std::string to_param = getQueryParam(query_string, "to");
            const std::string bucket_param = getQueryParam(query_string, "bucket");
            if (!from_param.empty() && !to_param.empty() && !bucket_param.empty()) {
                http::AnalyticsStatsHandler stats_handler(analyticsClickRepository());
                std::string body;
                int status = 500;
                stats_handler.Handle(slug, from_param, to_param, bucket_param, &body, &status,
                                     requestIdFromRequest(req));
                return makeResponse(req, config, is_tls, static_cast<unsigned>(status), body, "application/json");
            }
            const auto link = url_shortener::getLinkForRead(slug);
            if (!link.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
            }
            std::ostringstream body;
            body << "{\"slug\":" << url_shortener::jsonString(link->slug)
                 << ",\"total_redirects\":" << link->stats.total_redirects
                 << ",\"redirects_24h\":" << link->stats.redirects_24h
                 << ",\"redirects_7d\":" << link->stats.redirects_7d
                 << ",\"last_accessed_at\":";
            if (link->stats.last_accessed_at.has_value()) {
                body << url_shortener::jsonString(*link->stats.last_accessed_at);
            }
            else {
                body << "null";
            }
            body << ",\"created_at\":" << url_shortener::jsonString(link->created_at) << '}';
            return makeResponse(req, config, is_tls, 200, body.str(), "application/json");
        }

        if (action == "enable" || action == "disable" || action == "restore") {
            if (req.method() != bhttp::verb::post) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only POST is supported");
            }
            auto link = url_shortener::getLinkForRead(slug);
            if (!link.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
            }
            if (action == "enable") {
                link->enabled = true;
            }
            else if (action == "disable") {
                link->enabled = false;
            }
            else {
                link->deleted_at.reset();
            }
            link->updated_at = url_shortener::currentTimestamp();
            url_shortener::updateLinkAndInvalidateCache(*link);
            return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
        }

        if (action.empty()) {
            if (req.method() == bhttp::verb::get) {
                const auto link = url_shortener::getLinkForRead(slug);
                if (!link.has_value()) {
                    return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
                }
                return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
            }

            if (req.method() == bhttp::verb::delete_) {
                auto link = url_shortener::getLinkForRead(slug);
                if (!link.has_value()) {
                    return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
                }
                link->deleted_at = url_shortener::currentTimestamp();
                link->updated_at = url_shortener::currentTimestamp();
                url_shortener::updateLinkAndInvalidateCache(*link);
                return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
            }

            if (req.method() == bhttp::verb::patch) {
                auto link = url_shortener::getLinkForRead(slug);
                if (!link.has_value()) {
                    return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
                }

                if (url_shortener::hasJsonField(req.body(), "enabled")) {
                    const auto enabled = url_shortener::extractJsonBoolField(req.body(), "enabled");
                    if (!enabled.has_value()) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_enabled", "enabled must be boolean");
                    }
                    link->enabled = *enabled;
                }

                if (url_shortener::hasJsonField(req.body(), "expires_at")) {
                    const auto expires_at = url_shortener::extractJsonValueToken(req.body(), "expires_at");
                    if (!expires_at.has_value()) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_expires_at", "expires_at must be RFC3339 UTC or null");
                    }
                    if (*expires_at == "null") {
                        link->expires_at.reset();
                    }
                    else if (const auto parsed = url_shortener::extractJsonStringField(req.body(), "expires_at"); parsed.has_value() && url_shortener::parseRfc3339Zulu(*parsed).has_value()) {
                        link->expires_at = *parsed;
                    }
                    else {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_expires_at", "expires_at must be RFC3339 UTC or null");
                    }
                }

                if (url_shortener::hasJsonField(req.body(), "tags")) {
                    const auto tags = url_shortener::extractJsonStringArrayField(req.body(), "tags");
                    if (!tags.has_value()) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_tags", "tags must be string array");
                    }
                    auto normalized = *tags;
                    if (!validateTags(normalized)) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_tags", "tags violate constraints");
                    }
                    link->tags = std::move(normalized);
                }

                if (url_shortener::hasJsonField(req.body(), "metadata")) {
                    const auto metadata = url_shortener::extractJsonFlatObjectStringField(req.body(), "metadata");
                    if (!metadata.has_value() || !validateMetadata(*metadata)) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_metadata", "metadata must be flat object with string values");
                    }
                    link->metadata = *metadata;
                }

                if (url_shortener::hasJsonField(req.body(), "campaign")) {
                    const auto raw_campaign = url_shortener::extractJsonValueToken(req.body(), "campaign");
                    if (raw_campaign.has_value() && *raw_campaign == "null") {
                        link->campaign.reset();
                    }
                    else {
                        const auto campaign = extractCampaign(req.body());
                        if (!campaign.has_value()) {
                            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_campaign", "campaign must be object or null");
                        }
                        link->campaign = *campaign;
                        if (!validateCampaign(link->campaign)) {
                            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_campaign", "campaign fields exceed limits");
                        }
                    }
                }

                link->updated_at = url_shortener::currentTimestamp();
                url_shortener::updateLinkAndInvalidateCache(*link);
                return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
            }
        }

        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Unsupported operation");
    }

    if (target.rfind(std::string(shortener_api_compat_prefix) + '/', 0) == 0) {
        if (req.method() != bhttp::verb::get) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
        }
        const std::string slug = target.substr(std::string(shortener_api_compat_prefix).size() + 1);
        const auto link = url_shortener::getLinkForRead(slug);
        if (!link.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
        }
        return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
    }

    if (target == shortener_api_prefix || target == shortener_api_compat_prefix) {
        if (req.method() != bhttp::verb::post) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only POST is supported");
        }

        const auto raw_url = url_shortener::extractJsonStringField(req.body(), "url");
        if (!raw_url.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_url", "url is required");
        }
        const auto normalized_url = url_shortener::normalizeTargetUrl(*raw_url, config);
        if (!normalized_url.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_url", "url must be an absolute http/https URL");
        }

        std::string slug;
        if (const auto custom_slug = url_shortener::extractJsonStringField(req.body(), "slug"); custom_slug.has_value()) {
            if (!url_shortener::isValidSlug(*custom_slug)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_slug", "slug format is invalid");
            }
            if (url_shortener::isReservedSlug(*custom_slug)) {
                return makeApiErrorResponse(req, config, is_tls, 409, "reserved_slug", "slug is reserved");
            }
            if (linkRepository().slugExists(*custom_slug)) {
                return makeApiErrorResponse(req, config, is_tls, 409, "slug_conflict", "slug already in use");
            }
            slug = *custom_slug;
        }
        else if (const auto compat_code = url_shortener::extractJsonStringField(req.body(), "code"); compat_code.has_value()) {
            if (!url_shortener::isValidSlug(*compat_code)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_slug", "slug format is invalid");
            }
            if (url_shortener::isReservedSlug(*compat_code)) {
                return makeApiErrorResponse(req, config, is_tls, 409, "reserved_slug", "slug is reserved");
            }
            if (linkRepository().slugExists(*compat_code)) {
                return makeApiErrorResponse(req, config, is_tls, 409, "slug_conflict", "slug already in use");
            }
            slug = *compat_code;
        }
        else {
            const auto generated_slug = generateUniqueSlug(config);
            if (!generated_slug.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 500, "slug_generation_failed", "Unable to generate a unique slug");
            }
            slug = *generated_slug;
        }

        RedirectType redirect_type = RedirectType::temporary;
        if (const auto redirect_type_raw = url_shortener::extractJsonStringField(req.body(), "redirect_type"); redirect_type_raw.has_value()) {
            const auto parsed = url_shortener::parseRedirectType(*redirect_type_raw);
            if (!parsed.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_redirect_type", "redirect_type must be temporary or permanent");
            }
            redirect_type = *parsed;
        }
        else if (const auto parsed = url_shortener::parseRedirectType(config.shortener_default_redirect_type); parsed.has_value()) {
            redirect_type = *parsed;
        }

        std::optional<std::string> expires_at;
        if (const auto expires_at_raw = url_shortener::extractJsonStringField(req.body(), "expires_at"); expires_at_raw.has_value()) {
            if (!url_shortener::parseRfc3339Zulu(*expires_at_raw).has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_expires_at", "expires_at must be RFC3339 UTC");
            }
            expires_at = *expires_at_raw;
        }
        else if (config.shortener_default_expiry_seconds.has_value()) {
            expires_at = url_shortener::formatTimestamp(
                std::chrono::system_clock::now() + std::chrono::seconds(*config.shortener_default_expiry_seconds));
        }

        bool enabled = true;
        if (url_shortener::hasJsonField(req.body(), "enabled")) {
            const auto enabled_raw = url_shortener::extractJsonBoolField(req.body(), "enabled");
            if (!enabled_raw.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_enabled", "enabled must be boolean");
            }
            enabled = *enabled_raw;
        }

        std::vector<std::string> tags;
        if (url_shortener::hasJsonField(req.body(), "tags")) {
            const auto raw_tags = url_shortener::extractJsonStringArrayField(req.body(), "tags");
            if (!raw_tags.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_tags", "tags must be string array");
            }
            tags = *raw_tags;
            if (!validateTags(tags)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_tags", "tags violate constraints");
            }
        }

        std::unordered_map<std::string, std::string> metadata;
        if (url_shortener::hasJsonField(req.body(), "metadata")) {
            const auto raw_metadata = url_shortener::extractJsonFlatObjectStringField(req.body(), "metadata");
            if (!raw_metadata.has_value() || !validateMetadata(*raw_metadata)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_metadata", "metadata must be flat object with limits");
            }
            metadata = *raw_metadata;
        }

        std::optional<Link::Campaign> campaign;
        if (url_shortener::hasJsonField(req.body(), "campaign")) {
            const auto raw_campaign = url_shortener::extractJsonValueToken(req.body(), "campaign");
            if (raw_campaign.has_value() && *raw_campaign == "null") {
                campaign.reset();
            }
            else {
                campaign = extractCampaign(req.body());
                if (!campaign.has_value() || !validateCampaign(campaign)) {
                    return makeApiErrorResponse(req, config, is_tls, 400, "invalid_campaign", "campaign must be valid object");
                }
            }
        }

        if (url_shortener::hasJsonField(req.body(), "deleted_at")) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_request", "deleted_at is server-managed");
        }

        Link link;
        link.id = url_shortener::generateId();
        link.slug = slug;
        link.target_url = *normalized_url;
        link.created_at = url_shortener::currentTimestamp();
        link.updated_at = link.created_at;
        link.expires_at = expires_at;
        link.enabled = enabled;
        link.tags = std::move(tags);
        link.metadata = std::move(metadata);
        link.campaign = std::move(campaign);
        link.redirect_type = redirect_type;

        if (!linkRepository().create(link)) {
            return makeApiErrorResponse(req, config, is_tls, 409, "slug_conflict", "slug already in use");
        }
        url_shortener::linkCache().erase(link.slug);

        return makeResponse(req, config, is_tls, 201, serializeLink(link, makeShortUrl(req, config, is_tls, link.slug)), "application/json");
    }

    if (target.rfind(std::string(short_redirect_prefix), 0) == 0) {
        if (req.method() != bhttp::verb::get) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported for redirects");
        }
        const std::string slug = target.substr(short_redirect_prefix.size());
        if (!url_shortener::isValidSlug(slug)) {
            emitClickEvent(req, config, slug, std::nullopt, 404);
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Short code not found");
        }

        const auto link = url_shortener::getLinkForRead(slug);
        if (!link.has_value()) {
            emitClickEvent(req, config, slug, std::nullopt, 404);
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
        }

        const auto status = url_shortener::resolveLinkStatus(*link);
        if (status == LinkStatus::deleted) {
            emitClickEvent(req, config, slug, link, 404);
            return makeApiErrorResponse(req, config, is_tls, 404, "link_deleted", "Link not found");
        }
        if (status == LinkStatus::disabled) {
            emitClickEvent(req, config, slug, link, 410);
            return makeApiErrorResponse(req, config, is_tls, 410, "link_disabled", "Link is disabled");
        }
        if (status == LinkStatus::expired) {
            emitClickEvent(req, config, slug, link, 410);
            return makeApiErrorResponse(req, config, is_tls, 410, "link_expired", "Link has expired");
        }
        if (!passwordGuardCheck(*link, req) || !rateLimitGuardAllow(*link, req)) {
            return makeApiErrorResponse(req, config, is_tls, 429, "feature_not_enabled", "Request blocked by policy hook");
        }

        auto updated = *link;
        updated.stats.total_redirects++;
        updated.stats.redirects_24h++;
        updated.stats.redirects_7d++;
        updated.stats.last_accessed_at = url_shortener::currentTimestamp();
        url_shortener::updateLinkAndInvalidateCache(updated);

        const auto redirect_status = redirectStatusFor(*link);
        emitClickEvent(req, config, slug, link, static_cast<uint16_t>(redirect_status));

        bhttp::response<bhttp::string_body> res{redirect_status, req.version()};
        res.set(bhttp::field::server, "simple-http");
        res.set(bhttp::field::location, link->target_url);
        res.set(request_id_header, requestIdFromRequest(req));
        res.keep_alive(false);
        res.prepare_payload();
        return res;
    }

    if (target.size() > 1 && target[0] == '/' && target.rfind("/api/", 0) != 0) {
        if (req.method() != bhttp::verb::get) {
            return handleApplicationRequest(req, config, is_tls);
        }
        const std::string slug = target.substr(1);
        if (url_shortener::isValidSlug(slug)) {
            const auto link = url_shortener::getLinkForRead(slug);
            if (link.has_value()) {
                const auto status = url_shortener::resolveLinkStatus(*link);
                if (status == LinkStatus::active) {
                    auto updated = *link;
                    updated.stats.total_redirects++;
                    updated.stats.redirects_24h++;
                    updated.stats.redirects_7d++;
                    updated.stats.last_accessed_at = url_shortener::currentTimestamp();
                    url_shortener::updateLinkAndInvalidateCache(updated);

                    const auto redirect_status = redirectStatusFor(*link);
                    emitClickEvent(req, config, slug, link, static_cast<uint16_t>(redirect_status));

                    bhttp::response<bhttp::string_body> res{redirect_status, req.version()};
                    res.set(bhttp::field::server, "simple-http");
                    res.set(bhttp::field::location, link->target_url);
                    res.set(request_id_header, requestIdFromRequest(req));
                    res.keep_alive(false);
                    res.prepare_payload();
                    return res;
                }
                if (status == LinkStatus::deleted) {
                    emitClickEvent(req, config, slug, link, 404);
                    return makeApiErrorResponse(req, config, is_tls, 404, "link_deleted", "Link not found");
                }
                if (status == LinkStatus::disabled) {
                    emitClickEvent(req, config, slug, link, 410);
                    return makeApiErrorResponse(req, config, is_tls, 410, "link_disabled", "Link is disabled");
                }
                if (status == LinkStatus::expired) {
                    emitClickEvent(req, config, slug, link, 410);
                    return makeApiErrorResponse(req, config, is_tls, 410, "link_expired", "Link has expired");
                }
                emitClickEvent(req, config, slug, link, 404);
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
            }
            emitClickEvent(req, config, slug, std::nullopt, 404);
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
        }
    }

    return handleApplicationRequest(req, config, is_tls);
}

} // namespace url_shortener

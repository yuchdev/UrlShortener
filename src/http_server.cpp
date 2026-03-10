#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <deque>
#include <functional>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <http_server/http_server.h>
#include <http_server/uri_map_singleton.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace ssl = net::ssl;

namespace {

constexpr auto request_timeout = std::chrono::seconds(30);
constexpr std::string_view shortener_api_prefix = "/api/v1/links";
constexpr std::string_view shortener_api_compat_prefix = "/api/v1/short-urls";
constexpr std::string_view short_redirect_prefix = "/r/";
constexpr std::string_view request_id = "req_local";

enum class RedirectType
{
    temporary,
    permanent
};

struct Link
{
    struct Campaign
    {
        std::optional<std::string> name;
        std::optional<std::string> source;
        std::optional<std::string> medium;
        std::optional<std::string> term;
        std::optional<std::string> content;
        std::optional<std::string> id;
    };

    struct Stats
    {
        uint64_t total_redirects = 0;
        uint64_t redirects_24h = 0;
        uint64_t redirects_7d = 0;
        std::optional<std::string> last_accessed_at;
    };

    std::string id;
    std::string slug;
    std::string target_url;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> expires_at;
    std::optional<std::string> deleted_at;
    bool enabled = true;
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> metadata;
    std::optional<Campaign> campaign;
    Stats stats;
    RedirectType redirect_type = RedirectType::temporary;
};



struct ClickEvent
{
    std::string event_id;
    std::string occurred_at;
    std::string slug;
    std::optional<std::string> link_id;
    std::string domain;
    uint16_t status_code = 0;
    std::optional<std::string> referrer;
    std::optional<std::string> user_agent;
    std::string client_id_hash;
};

class BoundedClickEventQueue
{
public:
    explicit BoundedClickEventQueue(size_t capacity)
        : capacity_(capacity)
    {
    }

    void resetCapacity(size_t capacity)
    {
        std::unique_lock lock(mutex_);
        capacity_ = capacity;
        while (events_.size() > capacity_) {
            events_.pop_front();
            ++dropped_total_;
        }
    }

    bool tryEnqueue(ClickEvent event)
    {
        std::unique_lock lock(mutex_);
        if (events_.size() >= capacity_) {
            ++dropped_total_;
            return false;
        }
        events_.push_back(std::move(event));
        ++enqueued_total_;
        return true;
    }

private:
    std::mutex mutex_;
    size_t capacity_;
    std::deque<ClickEvent> events_;
    uint64_t enqueued_total_ = 0;
    uint64_t dropped_total_ = 0;
};

enum class LinkStatus
{
    active,
    disabled,
    expired,
    deleted
};

enum class RepoError
{
    not_found,
    already_exists,
    transient_failure,
    permanent_failure
};

class IMetadataRepository
{
public:
    virtual ~IMetadataRepository() = default;
    virtual bool create(const Link& link, RepoError* error = nullptr) = 0;
    virtual std::optional<Link> getBySlug(const std::string& slug, RepoError* error = nullptr) const = 0;
    virtual std::optional<Link> getById(const std::string& id, RepoError* error = nullptr) const = 0;
    virtual bool slugExists(const std::string& slug, RepoError* error = nullptr) const = 0;
    virtual bool update(const Link& link, RepoError* error = nullptr) = 0;
};

class ILinkCacheStore
{
public:
    virtual ~ILinkCacheStore() = default;
    virtual std::optional<Link> get(const std::string& slug) = 0;
    virtual void set(const std::string& slug, const Link& link) = 0;
    virtual void erase(const std::string& slug) = 0;
};

class InMemoryMetadataRepository final : public IMetadataRepository
{
public:
    bool create(const Link& link, RepoError* error = nullptr) override
    {
        std::unique_lock lock(mutex_);
        if (slug_to_id_.find(link.slug) != slug_to_id_.end() || by_id_.find(link.id) != by_id_.end()) {
            if (error != nullptr) {
                *error = RepoError::already_exists;
            }
            return false;
        }
        slug_to_id_[link.slug] = link.id;
        by_id_[link.id] = link;
        return true;
    }

    std::optional<Link> getBySlug(const std::string& slug, RepoError* error = nullptr) const override
    {
        std::shared_lock lock(mutex_);
        if (const auto slug_it = slug_to_id_.find(slug); slug_it != slug_to_id_.end()) {
            if (const auto id_it = by_id_.find(slug_it->second); id_it != by_id_.end()) {
                return id_it->second;
            }
        }
        if (error != nullptr) {
            *error = RepoError::not_found;
        }
        return std::nullopt;
    }

    std::optional<Link> getById(const std::string& id, RepoError* error = nullptr) const override
    {
        std::shared_lock lock(mutex_);
        if (const auto it = by_id_.find(id); it != by_id_.end()) {
            return it->second;
        }
        if (error != nullptr) {
            *error = RepoError::not_found;
        }
        return std::nullopt;
    }

    bool slugExists(const std::string& slug, RepoError* /*error*/ = nullptr) const override
    {
        std::shared_lock lock(mutex_);
        return slug_to_id_.find(slug) != slug_to_id_.end();
    }

    bool update(const Link& link, RepoError* error = nullptr) override
    {
        std::unique_lock lock(mutex_);
        const auto it = by_id_.find(link.id);
        if (it == by_id_.end()) {
            if (error != nullptr) {
                *error = RepoError::not_found;
            }
            return false;
        }
        it->second = link;
        return true;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Link> by_id_;
    std::unordered_map<std::string, std::string> slug_to_id_;
};

class InMemoryLinkCacheStore final : public ILinkCacheStore
{
public:
    std::optional<Link> get(const std::string& slug) override
    {
        std::shared_lock lock(mutex_);
        if (const auto it = by_slug_.find(slug); it != by_slug_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void set(const std::string& slug, const Link& link) override
    {
        std::unique_lock lock(mutex_);
        by_slug_[slug] = link;
    }

    void erase(const std::string& slug) override
    {
        std::unique_lock lock(mutex_);
        by_slug_.erase(slug);
    }

private:
    std::shared_mutex mutex_;
    std::unordered_map<std::string, Link> by_slug_;
};

IMetadataRepository& linkRepository()
{
    static InMemoryMetadataRepository repo;
    return repo;
}

ILinkCacheStore& linkCache()
{
    static InMemoryLinkCacheStore cache;
    return cache;
}

std::optional<Link> getLinkForRead(const std::string& slug)
{
    if (const auto cached = linkCache().get(slug); cached.has_value()) {
        return cached;
    }

    const auto link = linkRepository().getBySlug(slug);
    if (link.has_value()) {
        linkCache().set(slug, *link);
    }
    return link;
}

bool updateLinkAndInvalidateCache(const Link& link)
{
    if (!linkRepository().update(link)) {
        return false;
    }
    linkCache().erase(link.slug);
    return true;
}

BoundedClickEventQueue& analyticsQueue(const ServerConfig& config)
{
    static BoundedClickEventQueue queue(config.analytics_queue_capacity);
    static size_t last_capacity = config.analytics_queue_capacity;
    if (last_capacity != config.analytics_queue_capacity) {
        queue.resetCapacity(config.analytics_queue_capacity);
        last_capacity = config.analytics_queue_capacity;
    }
    return queue;
}

std::string clampString(const std::string& value, size_t max_len)
{
    if (value.size() <= max_len) {
        return value;
    }
    return value.substr(0, max_len);
}

std::string hexEncode(const unsigned char* data, size_t len)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        out << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return out.str();
}

std::string hashClientIdentifier(const std::string& raw_client, const std::string& salt)
{
    unsigned int len = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha256(),
        reinterpret_cast<const unsigned char*>(salt.data()),
        static_cast<int>(salt.size()),
        reinterpret_cast<const unsigned char*>(raw_client.data()),
        raw_client.size(),
        digest,
        &len);
    return hexEncode(digest, len);
}

std::optional<std::string> headerString(
    const http::request<http::string_body>& req,
    const http::field field,
    const size_t max_len)
{
    if (!req.base().count(field)) {
        return std::nullopt;
    }
    return clampString(std::string(req.base().at(field)), max_len);
}

void emitClickEvent(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const std::string& slug,
    const std::optional<Link>& link,
    const uint16_t status_code)
{
    if (!config.analytics_enabled) {
        return;
    }

    ClickEvent event;
    event.event_id = generateId();
    event.occurred_at = currentTimestamp();
    event.slug = slug;
    if (link.has_value()) {
        event.link_id = link->id;
    }

    std::string host = req.base().count(http::field::host) ? std::string(req.base().at(http::field::host)) : "";
    if (const auto pos = host.find(':'); pos != std::string::npos) {
        host = host.substr(0, pos);
    }
    event.domain = clampString(host, 255);
    event.status_code = status_code;
    event.referrer = headerString(req, http::field::referer, 512);
    event.user_agent = headerString(req, http::field::user_agent, 512);

    const std::string forwarded_for = req.base().count("X-Forwarded-For") ? std::string(req.base().at("X-Forwarded-For")) : "";
    const std::string client_source = forwarded_for.empty() ? "unknown" : clampString(forwarded_for, 128);
    event.client_id_hash = hashClientIdentifier(client_source, config.analytics_client_hash_salt);

    (void)analyticsQueue(config).tryEnqueue(std::move(event));
}

std::string trim(const std::string& value);
std::string currentTimestamp();
std::string generateId();

std::string jsonEscape(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (const char c : input) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string jsonString(const std::string& value)
{
    return '"' + jsonEscape(value) + '"';
}

std::optional<std::string> extractJsonStringField(const std::string& body, const std::string& field)
{
    const auto key = '"' + field + '"';
    const auto key_pos = body.find(key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    auto colon_pos = body.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    ++colon_pos;
    while (colon_pos < body.size() && std::isspace(static_cast<unsigned char>(body[colon_pos]))) {
        ++colon_pos;
    }
    if (colon_pos >= body.size() || body[colon_pos] != '"') {
        return std::nullopt;
    }

    ++colon_pos;
    std::string value;
    bool escape_next = false;
    for (auto i = colon_pos; i < body.size(); ++i) {
        const char c = body[i];
        if (escape_next) {
            value.push_back(c);
            escape_next = false;
            continue;
        }
        if (c == '\\') {
            escape_next = true;
            continue;
        }
        if (c == '"') {
            return value;
        }
        value.push_back(c);
    }

    return std::nullopt;
}

bool hasJsonField(const std::string& body, const std::string& field)
{
    const auto key = '"' + field + '"';
    return body.find(key) != std::string::npos;
}

std::optional<std::string> extractJsonValueToken(const std::string& body, const std::string& field)
{
    const auto key = '"' + field + '"';
    const auto key_pos = body.find(key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    auto colon_pos = body.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    ++colon_pos;
    while (colon_pos < body.size() && std::isspace(static_cast<unsigned char>(body[colon_pos]))) {
        ++colon_pos;
    }
    if (colon_pos >= body.size()) {
        return std::nullopt;
    }

    if (body[colon_pos] == 'n' && body.compare(colon_pos, 4, "null") == 0) {
        return std::string("null");
    }

    if (body[colon_pos] == '"') {
        const auto value = extractJsonStringField(body.substr(key_pos), field);
        return value;
    }

    if (body[colon_pos] == '[' || body[colon_pos] == '{') {
        const char open = body[colon_pos];
        const char close = open == '[' ? ']' : '}';
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        for (size_t i = colon_pos; i < body.size(); ++i) {
            const char c = body[i];
            if (in_string) {
                if (escape) {
                    escape = false;
                    continue;
                }
                if (c == '\\') {
                    escape = true;
                    continue;
                }
                if (c == '"') {
                    in_string = false;
                }
                continue;
            }
            if (c == '"') {
                in_string = true;
                continue;
            }
            if (c == open) {
                ++depth;
            }
            else if (c == close) {
                --depth;
                if (depth == 0) {
                    return body.substr(colon_pos, i - colon_pos + 1);
                }
            }
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::vector<std::string>> extractJsonStringArrayField(const std::string& body, const std::string& field)
{
    const auto token = extractJsonValueToken(body, field);
    if (!token.has_value()) {
        return std::nullopt;
    }
    if (*token == "null") {
        return std::vector<std::string>{};
    }
    const std::string& raw = *token;
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') {
        return std::nullopt;
    }

    std::vector<std::string> out;
    size_t i = 1;
    while (i < raw.size() - 1) {
        while (i < raw.size() - 1 && std::isspace(static_cast<unsigned char>(raw[i]))) {
            ++i;
        }
        if (i >= raw.size() - 1) {
            break;
        }
        if (raw[i] != '"') {
            return std::nullopt;
        }
        ++i;
        std::string value;
        bool escape = false;
        while (i < raw.size() - 1) {
            const char c = raw[i++];
            if (escape) {
                value.push_back(c);
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                break;
            }
            value.push_back(c);
        }
        out.push_back(value);
        while (i < raw.size() - 1 && std::isspace(static_cast<unsigned char>(raw[i]))) {
            ++i;
        }
        if (i < raw.size() - 1) {
            if (raw[i] != ',') {
                return std::nullopt;
            }
            ++i;
        }
    }
    return out;
}

std::optional<std::unordered_map<std::string, std::string>> extractJsonFlatObjectStringField(
    const std::string& body,
    const std::string& field)
{
    const auto token = extractJsonValueToken(body, field);
    if (!token.has_value()) {
        return std::nullopt;
    }
    if (*token == "null") {
        return std::unordered_map<std::string, std::string>{};
    }

    const std::string& raw = *token;
    if (raw.size() < 2 || raw.front() != '{' || raw.back() != '}') {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> out;
    static const std::regex pair_regex(R"re("([^"]+)"\s*:\s*"([^"]*)")re");
    for (auto it = std::sregex_iterator(raw.begin(), raw.end(), pair_regex); it != std::sregex_iterator(); ++it) {
        out[(*it)[1].str()] = (*it)[2].str();
    }

    const std::string without_pairs = std::regex_replace(raw, pair_regex, "");
    for (const char c : without_pairs) {
        if (!std::isspace(static_cast<unsigned char>(c)) && c != '{' && c != '}' && c != ',') {
            return std::nullopt;
        }
    }

    return out;
}

bool isValidHttpUrl(const std::string& url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool isValidSlug(const std::string& slug)
{
    static const std::regex slug_regex("^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$");
    if (!std::regex_match(slug, slug_regex)) {
        return false;
    }
    return true;
}

bool isReservedSlug(const std::string& slug)
{
    static const std::unordered_set<std::string> reserved = {
        "api", "health", "metrics", "admin", "login", "logout", "r", "preview", "stats"};
    std::string lower = slug;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char c) { return std::tolower(c); });
    return reserved.find(lower) != reserved.end();
}

std::optional<std::pair<std::string, std::string>> splitUrl(const std::string& input_url)
{
    static const std::regex url_regex(R"(^([A-Za-z][A-Za-z0-9+.-]*):\/\/([^\/\?#]+)(.*)$)");
    std::smatch match;
    if (!std::regex_match(input_url, match, url_regex)) {
        return std::nullopt;
    }
    return std::make_pair(match[1].str(), match[2].str() + match[3].str());
}

bool isPrivateHost(const std::string& host)
{
    const std::string lower_host = [&host]() {
        std::string out = host;
        std::transform(out.begin(), out.end(), out.begin(), [](const unsigned char c) { return std::tolower(c); });
        return out;
    }();
    return lower_host == "localhost" || lower_host == "::1" || lower_host.rfind("127.", 0) == 0
        || lower_host.rfind("10.", 0) == 0 || lower_host.rfind("192.168.", 0) == 0
        || lower_host.rfind("169.254.", 0) == 0 || lower_host.rfind("172.16.", 0) == 0
        || lower_host.rfind("172.17.", 0) == 0 || lower_host.rfind("172.18.", 0) == 0
        || lower_host.rfind("172.19.", 0) == 0 || lower_host.rfind("172.2", 0) == 0
        || lower_host.rfind("172.30.", 0) == 0 || lower_host.rfind("172.31.", 0) == 0;
}

std::optional<std::string> normalizeTargetUrl(const std::string& input_url, const ServerConfig& config)
{
    const std::string trimmed = trim(input_url);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    const auto split = splitUrl(trimmed);
    if (!split.has_value()) {
        return std::nullopt;
    }

    auto [scheme, rest] = *split;
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](const unsigned char c) { return std::tolower(c); });
    if (scheme != "http" && scheme != "https") {
        return std::nullopt;
    }

    const auto host_end = rest.find_first_of("/?#");
    std::string authority = host_end == std::string::npos ? rest : rest.substr(0, host_end);
    const std::string suffix = host_end == std::string::npos ? "" : rest.substr(host_end);

    if (authority.find('@') != std::string::npos) {
        return std::nullopt;
    }

    std::string host = authority;
    std::optional<std::string> port;
    if (const auto colon = authority.find(':'); colon != std::string::npos) {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
    }
    std::transform(host.begin(), host.end(), host.begin(), [](const unsigned char c) { return std::tolower(c); });
    if (host.empty()) {
        return std::nullopt;
    }
    if (!config.shortener_allow_private_targets && isPrivateHost(host)) {
        return std::nullopt;
    }

    if ((scheme == "http" && port == "80") || (scheme == "https" && port == "443")) {
        port.reset();
    }

    std::ostringstream out;
    out << scheme << "://" << host;
    if (port.has_value() && !port->empty()) {
        out << ':' << *port;
    }
    out << suffix;
    return out.str();
}

std::optional<bool> extractJsonBoolField(const std::string& body, const std::string& field)
{
    const auto key = '"' + field + '"';
    const auto key_pos = body.find(key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    auto colon_pos = body.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    ++colon_pos;
    while (colon_pos < body.size() && std::isspace(static_cast<unsigned char>(body[colon_pos]))) {
        ++colon_pos;
    }
    if (body.compare(colon_pos, 4, "true") == 0) {
        return true;
    }
    if (body.compare(colon_pos, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

std::optional<RedirectType> parseRedirectType(const std::string& value)
{
    if (value == "temporary") {
        return RedirectType::temporary;
    }
    if (value == "permanent") {
        return RedirectType::permanent;
    }
    return std::nullopt;
}

std::string redirectTypeToString(const RedirectType value)
{
    return value == RedirectType::permanent ? "permanent" : "temporary";
}

std::optional<std::chrono::system_clock::time_point> parseRfc3339Zulu(const std::string& value)
{
    std::tm tm{};
    std::istringstream in(value);
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (in.fail()) {
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

bool isExpired(const std::optional<std::string>& expires_at)
{
    if (!expires_at.has_value()) {
        return false;
    }
    const auto ts = parseRfc3339Zulu(*expires_at);
    if (!ts.has_value()) {
        return false;
    }
    return std::chrono::system_clock::now() >= *ts;
}

LinkStatus resolveLinkStatus(const Link& link)
{
    if (link.deleted_at.has_value()) {
        return LinkStatus::deleted;
    }
    if (!link.enabled) {
        return LinkStatus::disabled;
    }
    if (isExpired(link.expires_at)) {
        return LinkStatus::expired;
    }
    return LinkStatus::active;
}

std::string linkStatusToString(const LinkStatus status)
{
    switch (status) {
    case LinkStatus::deleted:
        return "deleted";
    case LinkStatus::disabled:
        return "disabled";
    case LinkStatus::expired:
        return "expired";
    case LinkStatus::active:
    default:
        return "active";
    }
}

bool validateTags(std::vector<std::string>& tags)
{
    if (tags.size() > 20) {
        return false;
    }
    static const std::regex tag_regex("^[a-zA-Z0-9:_-]+$");
    std::vector<std::string> normalized;
    normalized.reserve(tags.size());
    std::unordered_set<std::string> seen;
    for (auto& tag : tags) {
        const auto t = trim(tag);
        if (t.empty() || t.size() > 32 || !std::regex_match(t, tag_regex)) {
            return false;
        }
        if (seen.insert(t).second) {
            normalized.push_back(t);
        }
    }
    tags = std::move(normalized);
    return true;
}

bool validateMetadata(const std::unordered_map<std::string, std::string>& metadata)
{
    if (metadata.size() > 50) {
        return false;
    }
    for (const auto& [key, value] : metadata) {
        if (key.empty() || key.size() > 64 || value.size() > 512) {
            return false;
        }
    }
    return true;
}

bool validateCampaignField(const std::optional<std::string>& field)
{
    return !field.has_value() || field->size() <= 128;
}

bool validateCampaign(const std::optional<Link::Campaign>& campaign)
{
    if (!campaign.has_value()) {
        return true;
    }
    const auto& c = *campaign;
    return validateCampaignField(c.name) && validateCampaignField(c.source) && validateCampaignField(c.medium)
        && validateCampaignField(c.term) && validateCampaignField(c.content) && validateCampaignField(c.id);
}

bool passwordGuardCheck(const Link&, const http::request<http::string_body>&)
{
    return true;
}

bool rateLimitGuardAllow(const Link&, const http::request<http::string_body>&)
{
    return true;
}

std::string trim(const std::string& value)
{
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string generateSlug(const uint32_t length)
{
    static constexpr std::string_view chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, chars.size() - 1);

    std::string generated;
    generated.reserve(length);
    for (uint32_t i = 0; i < length; ++i) {
        generated.push_back(chars[dist(rng)]);
    }
    return generated;
}

std::string currentTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&now_time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string generateId()
{
    return generateSlug(20);
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

http::response<http::string_body> makeResponse(const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned status,
    const std::string& body,
    const std::optional<std::string>& content_type = std::nullopt)
{
    http::response<http::string_body> res{static_cast<http::status>(status), req.version()};
    res.set(http::field::server, "simple-http");
    res.keep_alive(false);
    if (content_type.has_value()) {
        res.set(http::field::content_type, *content_type);
    }

    if (is_tls && config.hsts_max_age.has_value()) {
        res.set(http::field::strict_transport_security,
            "max-age=" + std::to_string(*config.hsts_max_age) + "; includeSubDomains");
    }

    if (is_tls) {
        res.set("X-Content-Type-Options", "nosniff");
    }

    res.body() = body;
    res.prepare_payload();
    return res;
}

http::response<http::string_body> makeApiErrorResponse(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned status,
    const std::string& code,
    const std::string& message)
{
    std::ostringstream body;
    body << "{\"error\":{\"code\":" << jsonString(code)
         << ",\"message\":" << jsonString(message)
         << ",\"request_id\":" << jsonString(std::string(request_id))
         << "}}";
    return makeResponse(req, config, is_tls, status, body.str(), "application/json");
}

http::response<http::string_body> handleApplicationRequest(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls)
{
    const auto uri = std::string(req.target());

    if (req.method() == http::verb::get) {
        auto data = UriMapSingleton::getInstance().getData(uri);
        if (data.has_value()) {
            return makeResponse(req, config, is_tls, 200, data->first + "\n", data->second);
        }
        return makeResponse(req, config, is_tls, 404, "URI not found\n", "text/plain");
    }

    if (req.method() == http::verb::post) {
        const auto content_type = req[http::field::content_type].empty()
            ? "application/octet-stream"
            : std::string(req[http::field::content_type]);
        UriMapSingleton::getInstance().saveData(uri, req.body(), content_type);
        return makeResponse(req, config, is_tls, 200, "URI saved\n", "text/plain");
    }

    if (req.method() == http::verb::delete_) {
        if (UriMapSingleton::getInstance().deleteData(uri)) {
            return makeResponse(req, config, is_tls, 200, "Data deleted\n", "text/plain");
        }
        return makeResponse(req, config, is_tls, 404, "URI not found\n", "text/plain");
    }

    return makeResponse(req, config, is_tls, 400, "Invalid method\n", "text/plain");
}

std::string stripPort(std::string host)
{
    const auto pos = host.find(':');
    if (pos != std::string::npos) {
        host = host.substr(0, pos);
    }
    return host;
}

http::response<http::string_body> makeRedirectResponse(
    const http::request<http::string_body>& req,
    const ServerConfig& config)
{
    std::string host = req[http::field::host].empty() ? "localhost" : std::string(req[http::field::host]);
    host = stripPort(host);

    std::ostringstream location;
    location << "https://" << host;
    if (config.tls.port != 443) {
        location << ':' << config.tls.port;
    }
    location << req.target();

    http::response<http::string_body> res{http::status::permanent_redirect, req.version()};
    res.set(http::field::location, location.str());
    res.set(http::field::content_type, "text/plain");
    res.body() = "Use HTTPS\n";
    res.prepare_payload();
    return res;
}

std::string getHostWithoutPort(const http::request<http::string_body>& req)
{
    std::string host = req[http::field::host].empty() ? "localhost" : std::string(req[http::field::host]);
    return stripPort(host);
}

std::string makeShortUrl(const http::request<http::string_body>& req, const ServerConfig& config, const bool is_tls,
    const std::string& slug)
{
    std::string base = config.shortener_base_domain;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + "/" + slug;
}

std::optional<Link::Campaign> extractCampaign(const std::string& body)
{
    const auto raw = extractJsonValueToken(body, "campaign");
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
    if (const auto value = extractJsonStringField(*raw, "name"); value.has_value()) {
        campaign.name = *value;
    }
    if (const auto value = extractJsonStringField(*raw, "source"); value.has_value()) {
        campaign.source = *value;
    }
    if (const auto value = extractJsonStringField(*raw, "medium"); value.has_value()) {
        campaign.medium = *value;
    }
    if (const auto value = extractJsonStringField(*raw, "term"); value.has_value()) {
        campaign.term = *value;
    }
    if (const auto value = extractJsonStringField(*raw, "content"); value.has_value()) {
        campaign.content = *value;
    }
    if (const auto value = extractJsonStringField(*raw, "id"); value.has_value()) {
        campaign.id = *value;
    }
    return campaign;
}

std::string serializeLink(const Link& link, const std::string& short_url)
{
    std::ostringstream body;
    body << "{"
         << "\"id\":" << jsonString(link.id) << ","
         << "\"slug\":" << jsonString(link.slug) << ","
         << "\"short_url\":" << jsonString(short_url) << ","
         << "\"url\":" << jsonString(link.target_url) << ","
         << "\"redirect_type\":" << jsonString(redirectTypeToString(link.redirect_type)) << ","
         << "\"enabled\":" << (link.enabled ? "true" : "false") << ","
         << "\"deleted_at\":";
    if (link.deleted_at.has_value()) {
        body << jsonString(*link.deleted_at);
    }
    else {
        body << "null";
    }
    body << ","
         << "\"expires_at\":";
    if (link.expires_at.has_value()) {
        body << jsonString(*link.expires_at);
    }
    else {
        body << "null";
    }
    body << ",\"created_at\":" << jsonString(link.created_at)
         << ",\"updated_at\":" << jsonString(link.updated_at)
         << ",\"tags\":[";
    for (size_t i = 0; i < link.tags.size(); ++i) {
        if (i > 0) {
            body << ',';
        }
        body << jsonString(link.tags[i]);
    }
    body << "],\"metadata\":{";
    bool first = true;
    for (const auto& [key, value] : link.metadata) {
        if (!first) {
            body << ',';
        }
        body << jsonString(key) << ':' << jsonString(value);
        first = false;
    }
    body << "},\"campaign\":";
    if (link.campaign.has_value()) {
        const auto& c = *link.campaign;
        body << '{';
        bool first_campaign = true;
        auto add_field = [&](const std::string& key, const std::optional<std::string>& value) {
            if (!value.has_value()) {
                return;
            }
            if (!first_campaign) {
                body << ',';
            }
            body << jsonString(key) << ':' << jsonString(*value);
            first_campaign = false;
        };
        add_field("name", c.name);
        add_field("source", c.source);
        add_field("medium", c.medium);
        add_field("term", c.term);
        add_field("content", c.content);
        add_field("id", c.id);
        body << '}';
    }
    else {
        body << "null";
    }
    body << ",\"stats\":{"
         << "\"total_redirects\":" << link.stats.total_redirects << ','
         << "\"redirects_24h\":" << link.stats.redirects_24h << ','
         << "\"redirects_7d\":" << link.stats.redirects_7d << ','
         << "\"last_accessed_at\":";
    if (link.stats.last_accessed_at.has_value()) {
        body << jsonString(*link.stats.last_accessed_at);
    }
    else {
        body << "null";
    }
    body << "}"
         << "}";
    return body.str();
}

http::response<http::string_body> handleShortenerRequest(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls)
{
    const auto target = std::string(req.target());

    if (target.rfind(std::string(shortener_api_prefix) + "/id/", 0) == 0) {
        if (req.method() != http::verb::get) {
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
        const auto action_pos = resource.find('/');
        const std::string slug = action_pos == std::string::npos ? resource : resource.substr(0, action_pos);
        const std::string action = action_pos == std::string::npos ? "" : resource.substr(action_pos + 1);

        if (slug.empty()) {
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
        }

        if (action == "preview") {
            if (req.method() != http::verb::get) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
            }
            const auto link = getLinkForRead(slug);
            if (!link.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
            }
            const auto status = resolveLinkStatus(*link);
            std::ostringstream body;
            body << "{\"slug\":" << jsonString(link->slug)
                 << ",\"url\":" << jsonString(link->target_url)
                 << ",\"status\":" << jsonString(linkStatusToString(status))
                 << ",\"redirect_type\":" << jsonString(redirectTypeToString(link->redirect_type))
                 << ",\"enabled\":" << (link->enabled ? "true" : "false")
                 << ",\"expires_at\":";
            if (link->expires_at.has_value()) {
                body << jsonString(*link->expires_at);
            }
            else {
                body << "null";
            }
            body << ",\"deleted_at\":";
            if (link->deleted_at.has_value()) {
                body << jsonString(*link->deleted_at);
            }
            else {
                body << "null";
            }
            body << '}';
            return makeResponse(req, config, is_tls, 200, body.str(), "application/json");
        }

        if (action == "stats") {
            if (req.method() != http::verb::get) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
            }
            const auto link = getLinkForRead(slug);
            if (!link.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
            }
            std::ostringstream body;
            body << "{\"slug\":" << jsonString(link->slug)
                 << ",\"total_redirects\":" << link->stats.total_redirects
                 << ",\"redirects_24h\":" << link->stats.redirects_24h
                 << ",\"redirects_7d\":" << link->stats.redirects_7d
                 << ",\"last_accessed_at\":";
            if (link->stats.last_accessed_at.has_value()) {
                body << jsonString(*link->stats.last_accessed_at);
            }
            else {
                body << "null";
            }
            body << ",\"created_at\":" << jsonString(link->created_at) << '}';
            return makeResponse(req, config, is_tls, 200, body.str(), "application/json");
        }

        if (action == "enable" || action == "disable" || action == "restore") {
            if (req.method() != http::verb::post) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only POST is supported");
            }
            auto link = getLinkForRead(slug);
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
            link->updated_at = currentTimestamp();
            updateLinkAndInvalidateCache(*link);
            return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
        }

        if (action.empty()) {
            if (req.method() == http::verb::get) {
                const auto link = getLinkForRead(slug);
                if (!link.has_value()) {
                    return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
                }
                return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
            }

            if (req.method() == http::verb::delete_) {
                auto link = getLinkForRead(slug);
                if (!link.has_value()) {
                    return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
                }
                link->deleted_at = currentTimestamp();
                link->updated_at = currentTimestamp();
                updateLinkAndInvalidateCache(*link);
                return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
            }

            if (req.method() == http::verb::patch) {
                auto link = getLinkForRead(slug);
                if (!link.has_value()) {
                    return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
                }

                if (hasJsonField(req.body(), "enabled")) {
                    const auto enabled = extractJsonBoolField(req.body(), "enabled");
                    if (!enabled.has_value()) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_enabled", "enabled must be boolean");
                    }
                    link->enabled = *enabled;
                }

                if (hasJsonField(req.body(), "expires_at")) {
                    const auto expires_at = extractJsonValueToken(req.body(), "expires_at");
                    if (!expires_at.has_value()) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_expires_at", "expires_at must be RFC3339 UTC or null");
                    }
                    if (*expires_at == "null") {
                        link->expires_at.reset();
                    }
                    else if (const auto parsed = extractJsonStringField(req.body(), "expires_at"); parsed.has_value() && parseRfc3339Zulu(*parsed).has_value()) {
                        link->expires_at = *parsed;
                    }
                    else {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_expires_at", "expires_at must be RFC3339 UTC or null");
                    }
                }

                if (hasJsonField(req.body(), "tags")) {
                    const auto tags = extractJsonStringArrayField(req.body(), "tags");
                    if (!tags.has_value()) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_tags", "tags must be string array");
                    }
                    auto normalized = *tags;
                    if (!validateTags(normalized)) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_tags", "tags violate constraints");
                    }
                    link->tags = std::move(normalized);
                }

                if (hasJsonField(req.body(), "metadata")) {
                    const auto metadata = extractJsonFlatObjectStringField(req.body(), "metadata");
                    if (!metadata.has_value() || !validateMetadata(*metadata)) {
                        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_metadata", "metadata must be flat object with string values");
                    }
                    link->metadata = *metadata;
                }

                if (hasJsonField(req.body(), "campaign")) {
                    const auto raw_campaign = extractJsonValueToken(req.body(), "campaign");
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

                link->updated_at = currentTimestamp();
                updateLinkAndInvalidateCache(*link);
                return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
            }
        }

        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Unsupported operation");
    }

    if (target.rfind(std::string(shortener_api_compat_prefix) + '/', 0) == 0) {
        if (req.method() != http::verb::get) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported");
        }
        const std::string slug = target.substr(std::string(shortener_api_compat_prefix).size() + 1);
        const auto link = getLinkForRead(slug);
        if (!link.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
        }
        return makeResponse(req, config, is_tls, 200, serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)), "application/json");
    }

    if (target == shortener_api_prefix || target == shortener_api_compat_prefix) {
        if (req.method() != http::verb::post) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only POST is supported");
        }

        const auto raw_url = extractJsonStringField(req.body(), "url");
        if (!raw_url.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_url", "url is required");
        }
        const auto normalized_url = normalizeTargetUrl(*raw_url, config);
        if (!normalized_url.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_url", "url must be an absolute http/https URL");
        }

        std::string slug;
        if (const auto custom_slug = extractJsonStringField(req.body(), "slug"); custom_slug.has_value()) {
            if (!isValidSlug(*custom_slug)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_slug", "slug format is invalid");
            }
            if (isReservedSlug(*custom_slug)) {
                return makeApiErrorResponse(req, config, is_tls, 409, "reserved_slug", "slug is reserved");
            }
            if (linkRepository().slugExists(*custom_slug)) {
                return makeApiErrorResponse(req, config, is_tls, 409, "slug_conflict", "slug already in use");
            }
            slug = *custom_slug;
        }
        else if (const auto compat_code = extractJsonStringField(req.body(), "code"); compat_code.has_value()) {
            if (!isValidSlug(*compat_code)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_slug", "slug format is invalid");
            }
            if (isReservedSlug(*compat_code)) {
                return makeApiErrorResponse(req, config, is_tls, 409, "reserved_slug", "slug is reserved");
            }
            if (linkRepository().slugExists(*compat_code)) {
                return makeApiErrorResponse(req, config, is_tls, 409, "slug_conflict", "slug already in use");
            }
            slug = *compat_code;
        }
        else {
            bool created = false;
            for (int attempt = 0; attempt < 20; ++attempt) {
                const auto candidate = generateSlug(config.shortener_generated_slug_length);
                if (!isReservedSlug(candidate) && !linkRepository().slugExists(candidate)) {
                    slug = candidate;
                    created = true;
                    break;
                }
            }
            if (!created) {
                return makeApiErrorResponse(req, config, is_tls, 500, "slug_generation_failed", "Unable to generate a unique slug");
            }
        }

        RedirectType redirect_type = RedirectType::temporary;
        if (const auto redirect_type_raw = extractJsonStringField(req.body(), "redirect_type"); redirect_type_raw.has_value()) {
            const auto parsed = parseRedirectType(*redirect_type_raw);
            if (!parsed.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_redirect_type", "redirect_type must be temporary or permanent");
            }
            redirect_type = *parsed;
        }
        else if (const auto parsed = parseRedirectType(config.shortener_default_redirect_type); parsed.has_value()) {
            redirect_type = *parsed;
        }

        std::optional<std::string> expires_at;
        if (const auto expires_at_raw = extractJsonStringField(req.body(), "expires_at"); expires_at_raw.has_value()) {
            if (!parseRfc3339Zulu(*expires_at_raw).has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_expires_at", "expires_at must be RFC3339 UTC");
            }
            expires_at = *expires_at_raw;
        }

        bool enabled = true;
        if (hasJsonField(req.body(), "enabled")) {
            const auto enabled_raw = extractJsonBoolField(req.body(), "enabled");
            if (!enabled_raw.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_enabled", "enabled must be boolean");
            }
            enabled = *enabled_raw;
        }

        std::vector<std::string> tags;
        if (hasJsonField(req.body(), "tags")) {
            const auto raw_tags = extractJsonStringArrayField(req.body(), "tags");
            if (!raw_tags.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_tags", "tags must be string array");
            }
            tags = *raw_tags;
            if (!validateTags(tags)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_tags", "tags violate constraints");
            }
        }

        std::unordered_map<std::string, std::string> metadata;
        if (hasJsonField(req.body(), "metadata")) {
            const auto raw_metadata = extractJsonFlatObjectStringField(req.body(), "metadata");
            if (!raw_metadata.has_value() || !validateMetadata(*raw_metadata)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_metadata", "metadata must be flat object with limits");
            }
            metadata = *raw_metadata;
        }

        std::optional<Link::Campaign> campaign;
        if (hasJsonField(req.body(), "campaign")) {
            const auto raw_campaign = extractJsonValueToken(req.body(), "campaign");
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

        if (hasJsonField(req.body(), "deleted_at")) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_request", "deleted_at is server-managed");
        }

        Link link;
        link.id = generateId();
        link.slug = slug;
        link.target_url = *normalized_url;
        link.created_at = currentTimestamp();
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
        linkCache().erase(link.slug);

        return makeResponse(req, config, is_tls, 201, serializeLink(link, makeShortUrl(req, config, is_tls, link.slug)), "application/json");
    }

    if (target.rfind(std::string(short_redirect_prefix), 0) == 0) {
        if (req.method() != http::verb::get) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported for redirects");
        }
        const std::string slug = target.substr(short_redirect_prefix.size());
        if (!isValidSlug(slug)) {
            emitClickEvent(req, config, slug, std::nullopt, 404);
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Short code not found");
        }

        const auto link = getLinkForRead(slug);
        if (!link.has_value()) {
            emitClickEvent(req, config, slug, std::nullopt, 404);
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Link not found");
        }

        const auto status = resolveLinkStatus(*link);
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
        updated.stats.total_redirects += 1;
        updated.stats.redirects_24h += 1;
        updated.stats.redirects_7d += 1;
        updated.stats.last_accessed_at = currentTimestamp();
        updateLinkAndInvalidateCache(updated);

        const auto redirect_status =
            link->redirect_type == RedirectType::permanent ? http::status::moved_permanently : http::status::found;
        emitClickEvent(req, config, slug, link, static_cast<uint16_t>(redirect_status));

        http::response<http::string_body> res{redirect_status, req.version()};
        res.set(http::field::server, "simple-http");
        res.set(http::field::location, link->target_url);
        res.keep_alive(false);
        res.prepare_payload();
        return res;
    }

    if (target.size() > 1 && target[0] == '/' && target.rfind("/api/", 0) != 0) {
        if (req.method() != http::verb::get) {
            return handleApplicationRequest(req, config, is_tls);
        }
        const std::string slug = target.substr(1);
        if (isValidSlug(slug)) {
            const auto link = getLinkForRead(slug);
            if (link.has_value()) {
                const auto status = resolveLinkStatus(*link);
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
                updated.stats.total_redirects += 1;
                updated.stats.redirects_24h += 1;
                updated.stats.redirects_7d += 1;
                updated.stats.last_accessed_at = currentTimestamp();
                updateLinkAndInvalidateCache(updated);

                const auto redirect_status =
                    link->redirect_type == RedirectType::permanent ? http::status::moved_permanently : http::status::found;
                emitClickEvent(req, config, slug, link, static_cast<uint16_t>(redirect_status));

                http::response<http::string_body> res{redirect_status, req.version()};
                res.set(http::field::server, "simple-http");
                res.set(http::field::location, link->target_url);
                res.keep_alive(false);
                res.prepare_payload();
                return res;
            }
        }
        return handleApplicationRequest(req, config, is_tls);
    }

    return handleApplicationRequest(req, config, is_tls);
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

void configureTlsVersion(ssl::context& context, const std::string& min_version)
{
    auto* native = context.native_handle();
    SSL_CTX_set_min_proto_version(native, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(native, TLS1_3_VERSION);

    if (min_version == "TLS1.3") {
        SSL_CTX_set_min_proto_version(native, TLS1_3_VERSION);
    }
}

class PlainSession : public std::enable_shared_from_this<PlainSession>
{
public:
    PlainSession(tcp::socket socket, const ServerConfig& config)
        : socket_(std::move(socket))
        , config_(config)
    {
    }

    void run()
    {
        beast::get_lowest_layer(socket_).expires_after(request_timeout);
        http::async_read(socket_, buffer_, req_,
            beast::bind_front_handler(&PlainSession::onRead, shared_from_this()));
    }

private:
    void onRead(const beast::error_code& ec, std::size_t)
    {
        if (ec) {
            return;
        }

        if (config_.http_redirect_to_https && config_.tls.enabled) {
            res_ = makeRedirectResponse(req_, config_);
        }
        else {
            res_ = handleShortenerRequest(req_, config_, false);
        }

        http::async_write(socket_, res_, beast::bind_front_handler(&PlainSession::onWrite, shared_from_this()));
    }

    void onWrite(const beast::error_code&, std::size_t)
    {
        beast::error_code ignored;
        socket_.socket().shutdown(tcp::socket::shutdown_send, ignored);
    }

    beast::tcp_stream socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    http::response<http::string_body> res_;
    const ServerConfig& config_;
};

class TlsSession : public std::enable_shared_from_this<TlsSession>
{
public:
    TlsSession(tcp::socket socket,
        std::shared_ptr<ssl::context> context,
        const ServerConfig& config,
        std::atomic<uint64_t>& success_counter,
        std::atomic<uint64_t>& failure_counter)
        : stream_(std::move(socket), *context)
        , config_(config)
        , success_counter_(success_counter)
        , failure_counter_(failure_counter)
    {
    }

    void run()
    {
        beast::get_lowest_layer(stream_).expires_after(request_timeout);
        stream_.async_handshake(ssl::stream_base::server,
            beast::bind_front_handler(&TlsSession::onHandshake, shared_from_this()));
    }

private:
    void onHandshake(const beast::error_code& ec)
    {
        if (ec) {
            ++failure_counter_;
            std::cerr << "TLS handshake failed: " << ec.message() << '\n';
            return;
        }

        ++success_counter_;
        auto* ssl_handle = stream_.native_handle();
        std::cout << "TLS handshake success"
                  << " | version=" << SSL_get_version(ssl_handle)
                  << " | cipher=" << SSL_get_cipher_name(ssl_handle)
                  << '\n';

        beast::get_lowest_layer(stream_).expires_after(request_timeout);
        http::async_read(stream_, buffer_, req_,
            beast::bind_front_handler(&TlsSession::onRead, shared_from_this()));
    }

    void onRead(const beast::error_code& ec, std::size_t)
    {
        if (ec) {
            return;
        }

        res_ = handleShortenerRequest(req_, config_, true);
        http::async_write(stream_, res_,
            beast::bind_front_handler(&TlsSession::onWrite, shared_from_this()));
    }

    void onWrite(const beast::error_code&, std::size_t)
    {
        beast::get_lowest_layer(stream_).expires_after(request_timeout);
        stream_.async_shutdown(
            beast::bind_front_handler(&TlsSession::onShutdown, shared_from_this()));
    }

    void onShutdown(const beast::error_code&)
    {
        beast::get_lowest_layer(stream_).close();
    }

    ssl::stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    http::response<http::string_body> res_;
    const ServerConfig& config_;
    std::atomic<uint64_t>& success_counter_;
    std::atomic<uint64_t>& failure_counter_;
};

} // namespace

TlsConfig::ClientAuthMode parseClientAuthMode(const std::string& value)
{
    if (value == "optional") {
        return TlsConfig::ClientAuthMode::optional;
    }
    if (value == "required") {
        return TlsConfig::ClientAuthMode::required;
    }
    return TlsConfig::ClientAuthMode::none;
}

HttpServer::HttpServer(net::io_context& io_context, ServerConfig config)
    : io_context_(io_context)
    , config_(std::move(config))
{
}

boost::asio::ssl::context HttpServer::buildTlsContext() const
{
    ssl::context context{ssl::context::tls_server};
    context.set_options(
        ssl::context::default_workarounds |
        ssl::context::no_sslv2 |
        ssl::context::no_sslv3 |
        ssl::context::no_tlsv1 |
        ssl::context::no_tlsv1_1 |
        ssl::context::single_dh_use);

    if (config_.tls.certificate_chain_file.empty() || config_.tls.private_key_file.empty()) {
        throw std::runtime_error("TLS enabled but certificate or key file is missing");
    }

    validatePrivateKeyFilePermissions(config_.tls.private_key_file);

    if (config_.tls.private_key_passphrase.has_value()) {
        context.set_password_callback([this](std::size_t, ssl::context::password_purpose) {
            return *config_.tls.private_key_passphrase;
        });
    }

    configureTlsVersion(context, config_.tls.min_version);

    if (SSL_CTX_set_ciphersuites(context.native_handle(), config_.tls.cipher_suites.c_str()) != 1) {
        throw std::runtime_error("Failed to set TLS 1.3 cipher suites");
    }

    if (SSL_CTX_set_cipher_list(context.native_handle(), config_.tls.ciphers.c_str()) != 1) {
        throw std::runtime_error("Failed to set TLS 1.2 cipher list");
    }

    SSL_CTX_set1_groups_list(context.native_handle(), config_.tls.curves.c_str());

    (void)config_.tls.alpn;

    if (!config_.tls.session_tickets) {
        SSL_CTX_set_options(context.native_handle(), SSL_OP_NO_TICKET);
    }

    if (config_.tls.session_cache) {
        SSL_CTX_set_session_cache_mode(context.native_handle(), SSL_SESS_CACHE_SERVER);
    }
    else {
        SSL_CTX_set_session_cache_mode(context.native_handle(), SSL_SESS_CACHE_OFF);
    }

    context.use_certificate_chain_file(config_.tls.certificate_chain_file);
    context.use_private_key_file(config_.tls.private_key_file, ssl::context::pem);
    if (!SSL_CTX_check_private_key(context.native_handle())) {
        throw std::runtime_error("Certificate and private key do not match");
    }

    if (config_.tls.ca_file.has_value()) {
        context.load_verify_file(*config_.tls.ca_file);
    }
    if (config_.tls.ca_path.has_value()) {
        SSL_CTX_load_verify_locations(context.native_handle(), nullptr, config_.tls.ca_path->c_str());
    }

    switch (config_.tls.client_auth_mode) {
    case TlsConfig::ClientAuthMode::none:
        context.set_verify_mode(ssl::verify_none);
        break;
    case TlsConfig::ClientAuthMode::optional:
        context.set_verify_mode(ssl::verify_peer);
        break;
    case TlsConfig::ClientAuthMode::required:
        context.set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
        break;
    }

    return context;
}

void HttpServer::run()
{
    if (config_.http_enabled) {
        http_acceptor_ = std::make_unique<tcp::acceptor>(io_context_, tcp::endpoint(tcp::v4(), config_.http_port));
        auto do_accept_http = std::make_shared<std::function<void()>>();
        *do_accept_http = [this, do_accept_http]() {
            http_acceptor_->async_accept([this, do_accept_http](const beast::error_code& ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<PlainSession>(std::move(socket), config_)->run();
                }
                (*do_accept_http)();
            });
        };
        (*do_accept_http)();
        std::cout << "HTTP listener started on port " << config_.http_port << '\n';
    }

    if (config_.tls.enabled) {
        tls_context_ = std::make_shared<ssl::context>(buildTlsContext());
        const auto days = certExpiryDaysRemaining(config_.tls.certificate_chain_file);
        std::cout << "Certificate expires in " << days << " day(s)" << '\n';

        https_acceptor_ = std::make_unique<tcp::acceptor>(io_context_, tcp::endpoint(tcp::v4(), config_.tls.port));
        auto do_accept_https = std::make_shared<std::function<void()>>();
        *do_accept_https = [this, do_accept_https]() {
            https_acceptor_->async_accept([this, do_accept_https](const beast::error_code& ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<TlsSession>(
                        std::move(socket), tls_context_, config_, tls_handshake_success_count_, tls_handshake_failure_count_)
                        ->run();
                }
                (*do_accept_https)();
            });
        };
        (*do_accept_https)();
        std::cout << "HTTPS listener started on port " << config_.tls.port << '\n';
    }
}

void HttpServer::stop()
{
    beast::error_code ec;
    if (http_acceptor_ != nullptr) {
        http_acceptor_->cancel(ec);
        http_acceptor_->close(ec);
    }
    if (https_acceptor_ != nullptr) {
        https_acceptor_->cancel(ec);
        https_acceptor_->close(ec);
    }

    std::cout << "TLS metrics: success=" << tls_handshake_success_count_.load()
              << " failure=" << tls_handshake_failure_count_.load() << '\n';
}

void HttpServer::reloadTlsContext()
{
    if (!config_.tls.enabled) {
        return;
    }

    tls_context_ = std::make_shared<ssl::context>(buildTlsContext());
    std::cout << "TLS context reloaded successfully" << '\n';
}

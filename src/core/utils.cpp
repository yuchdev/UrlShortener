#include <optional>
#include <vector>
#include <string_view>
#include <url_shortener/core/utils.h>
#include <url_shortener/core/config.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace url_shortener {

std::time_t timegm_utc(std::tm* tm)
{
#if defined(_WIN32)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

void gmtime_utc(const std::time_t* t, std::tm* tm)
{
#if defined(_WIN32)
    gmtime_s(tm, t);
#else
    gmtime_r(t, tm);
#endif
}

std::string currentTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_utc(&now_time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string formatTimestamp(const std::chrono::system_clock::time_point time_point)
{
    const std::time_t ts = std::chrono::system_clock::to_time_t(time_point);
    std::tm tm{};
    gmtime_utc(&ts, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::optional<std::chrono::system_clock::time_point> parseRfc3339Zulu(const std::string& value)
{
    std::tm tm{};
    std::istringstream in(value);
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (in.fail()) {
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(timegm_utc(&tm));
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
    return *ts < std::chrono::system_clock::now();
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

std::string generateId()
{
    return generateSlug(20);
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
    if (lower_host == "localhost" || lower_host == "::1" || lower_host == "::" || lower_host == "0.0.0.0"
        || lower_host == "[::1]" || lower_host == "[::]") {
        return true;
    }
    if (lower_host == "internal" || lower_host == "localhost.localdomain"
        || lower_host.find(".internal") != std::string::npos
        || lower_host.find(".local") != std::string::npos) {
        return true;
    }
    return lower_host == "localhost" || lower_host == "::1" || lower_host.rfind("127.", 0) == 0
        || lower_host.rfind("10.", 0) == 0 || lower_host.rfind("192.168.", 0) == 0
        || lower_host.rfind("169.254.", 0) == 0 || lower_host.rfind("172.16.", 0) == 0
        || lower_host.rfind("172.17.", 0) == 0 || lower_host.rfind("172.18.", 0) == 0
        || lower_host.rfind("172.19.", 0) == 0 || lower_host.rfind("172.2", 0) == 0
        || lower_host.rfind("172.30.", 0) == 0 || lower_host.rfind("172.31.", 0) == 0
        || lower_host.rfind("fc", 0) == 0 || lower_host.rfind("fd", 0) == 0
        || lower_host.rfind("fe80:", 0) == 0;
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

std::string normalizeAndValidateBaseDomain(const std::string& raw_domain)
{
    const std::string trimmed = trim(raw_domain);
    if (trimmed.empty()) {
        throw std::runtime_error("SHORTENER_BASE_DOMAIN must not be empty");
    }
    const auto split = splitUrl(trimmed);
    if (!split.has_value()) {
        throw std::runtime_error("SHORTENER_BASE_DOMAIN must include http:// or https:// and a host");
    }
    auto [scheme, authority_and_rest] = *split;
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](const unsigned char c) { return std::tolower(c); });
    if (scheme != "http" && scheme != "https") {
        throw std::runtime_error("SHORTENER_BASE_DOMAIN scheme must be http or https");
    }

    while (!authority_and_rest.empty() && authority_and_rest.back() == '/') {
        authority_and_rest.pop_back();
    }
    const auto sep = authority_and_rest.find_first_of("/?#");
    if (sep != std::string::npos) {
        throw std::runtime_error("SHORTENER_BASE_DOMAIN must not include path, query, or fragment");
    }
    if (authority_and_rest.empty()) {
        throw std::runtime_error("SHORTENER_BASE_DOMAIN host is required");
    }

    return scheme + "://" + authority_and_rest;
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

} // namespace url_shortener

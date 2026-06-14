#pragma once

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <ctime>
#include <boost/beast/http.hpp>
#include <url_shortener/core/types.h>
#include <url_shortener/core/config.h>

namespace url_shortener {

// Forward declaration
namespace http = boost::beast::http;

// Time utilities
std::time_t timegm_utc(std::tm* tm);
void gmtime_utc(const std::time_t* t, std::tm* tm);
std::string currentTimestamp();
std::string formatTimestamp(const std::chrono::system_clock::time_point time_point);
std::optional<std::chrono::system_clock::time_point> parseRfc3339Zulu(const std::string& value);
bool isExpired(const std::optional<std::string>& expires_at);

// String manipulation
std::string trim(const std::string& value);
std::string clampString(const std::string& value, size_t max_len);
std::string hexEncode(const unsigned char* data, size_t len);

// ID generation
std::string generateSlug(const uint32_t length);
std::string generateId();

// Validation and normalization
bool isValidHttpUrl(const std::string& url);
bool isValidSlug(const std::string& slug);
bool isReservedSlug(const std::string& slug);
std::optional<std::pair<std::string, std::string>> splitUrl(const std::string& input_url);
bool isPrivateHost(const std::string& host);
std::optional<std::string> normalizeTargetUrl(const std::string& input_url, const ServerConfig& config);
std::string normalizeAndValidateBaseDomain(const std::string& raw_domain);

// Cryptography helpers
std::string hashClientIdentifier(const std::string& raw_client, const std::string& salt);

// HTTP helpers
std::optional<std::string> headerString(
    const http::request<http::string_body>& req,
    const http::field field,
    const size_t max_len);

// JSON helpers
std::string jsonEscape(const std::string& input);
std::string jsonString(const std::string& value);
std::optional<std::string> extractJsonStringField(const std::string& body, const std::string& field);
std::optional<bool> extractJsonBoolField(const std::string& body, const std::string& field);
bool hasJsonField(const std::string& body, const std::string& field);
std::optional<std::string> extractJsonValueToken(const std::string& body, const std::string& field);
std::optional<std::vector<std::string>> extractJsonStringArrayField(const std::string& body, const std::string& field);
std::optional<std::unordered_map<std::string, std::string>> extractJsonFlatObjectStringField(
    const std::string& body,
    const std::string& field);

// Enum conversions
std::optional<RedirectType> parseRedirectType(const std::string& value);
std::string redirectTypeToString(const RedirectType value);
std::string linkStatusToString(const LinkStatus status);

// Link utilities
LinkStatus resolveLinkStatus(const Link& link);

} // namespace url_shortener

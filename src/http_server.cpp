#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <functional>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <http_server/http_server.h>
#include <http_server/uri_map_singleton.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace ssl = net::ssl;

namespace {

constexpr auto request_timeout = std::chrono::seconds(30);
constexpr std::string_view shortener_api_prefix = "/api/v1/short-urls";
constexpr std::string_view short_redirect_prefix = "/r/";

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

bool isValidHttpUrl(const std::string& url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool isValidCode(const std::string& code)
{
    if (code.size() < 3 || code.size() > 32) {
        return false;
    }
    return std::all_of(code.begin(), code.end(), [](const char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
}

std::string codeStoragePath(const std::string& code)
{
    return std::string(short_redirect_prefix) + code;
}

std::string makeStoredMappingPayload(const std::string& code, const std::string& long_url)
{
    std::ostringstream payload;
    payload << "{"
            << "\"code\":" << jsonString(code) << ","
            << "\"url\":" << jsonString(long_url) << ","
            << "\"meta\":{}"
            << "}";
    return payload.str();
}

std::optional<std::string> loadShortCodeTarget(const std::string& code)
{
    const auto stored = UriMapSingleton::getInstance().getData(codeStoragePath(code));
    if (!stored.has_value()) {
        return std::nullopt;
    }
    return extractJsonStringField(stored->first, "url");
}

bool storeShortCode(const std::string& code, const std::string& long_url)
{
    UriMapSingleton::getInstance().saveData(
        codeStoragePath(code), makeStoredMappingPayload(code, long_url), "application/json");
    return true;
}

bool deleteShortCode(const std::string& code)
{
    return UriMapSingleton::getInstance().deleteData(codeStoragePath(code));
}

std::string generateShortCode()
{
    static constexpr std::string_view chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static constexpr int length = 6;
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, chars.size() - 1);

    std::string generated;
    generated.reserve(length);
    for (int i = 0; i < length; ++i) {
        generated.push_back(chars[dist(rng)]);
    }
    return generated;
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
        res.set(http::field::x_content_type_options, "nosniff");
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
         << ",\"message\":" << jsonString(message) << "}}";
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
    const std::string& code)
{
    std::ostringstream out;
    out << (is_tls ? "https://" : "http://") << getHostWithoutPort(req);
    const auto port = is_tls ? config.tls.port : config.http_port;
    if ((is_tls && port != 443) || (!is_tls && port != 80)) {
        out << ':' << port;
    }
    out << short_redirect_prefix << code;
    return out.str();
}

http::response<http::string_body> handleShortenerRequest(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls)
{
    const auto target = std::string(req.target());

    if (target.rfind(std::string(short_redirect_prefix), 0) == 0) {
        if (req.method() != http::verb::get) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only GET is supported for redirects");
        }
        const std::string code = target.substr(short_redirect_prefix.size());
        if (!isValidCode(code)) {
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Short code not found");
        }

        const auto long_url = loadShortCodeTarget(code);
        if (!long_url.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Short code not found");
        }

        http::response<http::string_body> res{http::status::found, req.version()};
        res.set(http::field::server, "simple-http");
        res.set(http::field::location, *long_url);
        res.keep_alive(false);
        res.prepare_payload();
        return res;
    }

    if (target == shortener_api_prefix) {
        if (req.method() != http::verb::post) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Only POST is supported");
        }

        const auto url = extractJsonStringField(req.body(), "url");
        if (!url.has_value() || !isValidHttpUrl(*url)) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_url", "Field 'url' must be an absolute http(s) URL");
        }

        std::string code;
        const auto requested_code = extractJsonStringField(req.body(), "code");
        if (hasJsonField(req.body(), "code") && !requested_code.has_value()) {
            return makeApiErrorResponse(req, config, is_tls, 400, "invalid_code", "Field 'code' must be a string");
        }

        if (requested_code.has_value()) {
            code = *requested_code;
            if (!isValidCode(code)) {
                return makeApiErrorResponse(req, config, is_tls, 400, "invalid_code", "Field 'code' has invalid format");
            }
            if (loadShortCodeTarget(code).has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 409, "code_conflict", "Short code is already in use");
            }
        }
        else {
            constexpr int max_attempts = 20;
            bool created = false;
            for (int attempt = 0; attempt < max_attempts; ++attempt) {
                auto candidate = generateShortCode();
                if (!loadShortCodeTarget(candidate).has_value()) {
                    code = std::move(candidate);
                    created = true;
                    break;
                }
            }
            if (!created) {
                return makeApiErrorResponse(req, config, is_tls, 500, "code_generation_failed", "Unable to generate unique short code");
            }
        }

        storeShortCode(code, *url);
        std::ostringstream body;
        body << "{"
             << "\"code\":" << jsonString(code) << ","
             << "\"short_url\":" << jsonString(makeShortUrl(req, config, is_tls, code)) << ","
             << "\"url\":" << jsonString(*url) << ","
             << "\"meta\":{}"
             << "}";
        return makeResponse(req, config, is_tls, 201, body.str(), "application/json");
    }

    if (target.rfind(std::string(shortener_api_prefix) + '/', 0) == 0) {
        const std::string code = target.substr(shortener_api_prefix.size() + 1);
        if (!isValidCode(code)) {
            return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Short code not found");
        }

        if (req.method() == http::verb::get) {
            const auto long_url = loadShortCodeTarget(code);
            if (!long_url.has_value()) {
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Short code not found");
            }

            std::ostringstream body;
            body << "{"
                 << "\"code\":" << jsonString(code) << ","
                 << "\"short_url\":" << jsonString(makeShortUrl(req, config, is_tls, code)) << ","
                 << "\"url\":" << jsonString(*long_url) << ","
                 << "\"meta\":{}"
                 << "}";
            return makeResponse(req, config, is_tls, 200, body.str(), "application/json");
        }

        if (req.method() == http::verb::delete_) {
            if (!deleteShortCode(code)) {
                return makeApiErrorResponse(req, config, is_tls, 404, "not_found", "Short code not found");
            }
            return makeResponse(req, config, is_tls, 204, "", std::nullopt);
        }

        return makeApiErrorResponse(req, config, is_tls, 400, "invalid_method", "Unsupported method for short URL resource");
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
        socket_.shutdown(tcp::socket::shutdown_send, ignored);
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
        beast::error_code ignored;
        stream_.shutdown(ignored);
    }

    beast::ssl_stream<beast::tcp_stream> stream_;
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

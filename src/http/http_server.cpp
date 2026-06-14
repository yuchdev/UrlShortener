#include <url_shortener/http/http_server.h>
#include <url_shortener/http/http_session.h>
#include <url_shortener/http/request_handlers.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/core/config.h>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace ssl = net::ssl;

namespace url_shortener {

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
    config_.shortener_base_domain = normalizeAndValidateBaseDomain(config_.shortener_base_domain);
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
        logStructured("INFO", "http_listener_started", {{"component","http_server"},{"port", std::to_string(config_.http_port)}});
    }

    if (config_.tls.enabled) {
        tls_context_ = std::make_shared<ssl::context>(buildTlsContext());
        const auto days = certExpiryDaysRemaining(config_.tls.certificate_chain_file);
        logStructured("INFO", "tls_certificate_expiry", {{"component","tls"},{"days", std::to_string(days)}});

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
        logStructured("INFO", "https_listener_started", {{"component","tls"},{"port", std::to_string(config_.tls.port)}});
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

    logStructured("INFO", "tls_metrics", {{"component","tls"},{"success", std::to_string(tls_handshake_success_count_.load())},{"failure", std::to_string(tls_handshake_failure_count_.load())}});
}

void HttpServer::reloadTlsContext()
{
    if (!config_.tls.enabled) {
        return;
    }
    try {
        tls_context_ = std::make_shared<ssl::context>(buildTlsContext());
        metricsRegistry().tls_reload_success_total.fetch_add(1);
        logStructured("INFO", "tls context reloaded", { {"component", "tls"} });
    }
    catch (...) {
        metricsRegistry().tls_reload_failure_total.fetch_add(1);
        throw;
    }
}

} // namespace url_shortener

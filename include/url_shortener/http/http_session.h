#pragma once

#include <memory>
#include <atomic>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <boost/asio/ssl/stream.hpp>

struct ServerConfig;

namespace url_shortener {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace ssl = net::ssl;

class PlainSession : public std::enable_shared_from_this<PlainSession>
{
public:
    PlainSession(tcp::socket socket, const ServerConfig& config);
    void run();

private:
    void onRead(const beast::error_code& ec, std::size_t);
    void onWrite(const beast::error_code&, std::size_t);

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
        std::atomic<uint64_t>& failure_counter);
    void run();

private:
    void onHandshake(const beast::error_code& ec);
    void onRead(const beast::error_code& ec, std::size_t);
    void onWrite(const beast::error_code&, std::size_t);
    void onShutdown(const beast::error_code&);

    ssl::stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    http::response<http::string_body> res_;
    const ServerConfig& config_;
    std::atomic<uint64_t>& success_counter_;
    std::atomic<uint64_t>& failure_counter_;
};

} // namespace url_shortener

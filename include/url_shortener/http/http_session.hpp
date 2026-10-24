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

class PlainSession : public std::enable_shared_from_this<PlainSession>
{
public:
    PlainSession(boost::asio::ip::tcp::socket socket, const ServerConfig& config);
    void run();

private:
    void onRead(const boost::beast::error_code& ec, std::size_t);
    void onWrite(const boost::beast::error_code&, std::size_t);

    boost::beast::tcp_stream socket_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request<boost::beast::http::string_body> req_;
    boost::beast::http::response<boost::beast::http::string_body> res_;
    const ServerConfig& config_;
};

class TlsSession : public std::enable_shared_from_this<TlsSession>
{
public:
    TlsSession(boost::asio::ip::tcp::socket socket,
        std::shared_ptr<boost::asio::ssl::context> context,
        const ServerConfig& config,
        std::atomic<uint64_t>& success_counter,
        std::atomic<uint64_t>& failure_counter);
    void run();

private:
    void onHandshake(const boost::beast::error_code& ec);
    void onRead(const boost::beast::error_code& ec, std::size_t);
    void onWrite(const boost::beast::error_code&, std::size_t);
    void onShutdown(const boost::beast::error_code&);

    boost::asio::ssl::stream<boost::beast::tcp_stream> stream_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request<boost::beast::http::string_body> req_;
    boost::beast::http::response<boost::beast::http::string_body> res_;
    const ServerConfig& config_;
    std::atomic<uint64_t>& success_counter_;
    std::atomic<uint64_t>& failure_counter_;
};

} // namespace url_shortener

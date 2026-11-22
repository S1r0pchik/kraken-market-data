#include "kraken_client.hpp"
#include "../../nlohmann/json.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <openssl/ssl.h>

#include <string_view>
#include <utility>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using net::ip::tcp;
using nlohmann::json;

using namespace std::chrono_literals;
constexpr auto OPERATION_TIMEOUT = 10s;

template <typename Start, typename Cancel>
void with_deadline(net::io_context& io, std::chrono::milliseconds timeout, std::string_view stage,
                   Start start, Cancel cancel) {
    io.restart();
    net::steady_timer timer(io, timeout);
    boost::system::error_code result;
    bool complete = false;
    bool timed_out = false;

    start([&](boost::system::error_code error, auto&&...) {
        result = error;
        complete = true;
        timer.cancel();
    });
    timer.async_wait([&](boost::system::error_code error) {
        if (!error && !complete) {
            timed_out = true;
            cancel();
        }
    });
    io.run();

    if (timed_out) {
        throw ConnectionError(std::string(stage) + " timed out");
    }
    if (result) {
        throw ConnectionError(std::string(stage) + ": " + result.message());
    }
}

} // namespace

struct KrakenWebSocketClient::Impl {
    net::io_context io;
    net::ssl::context tls{net::ssl::context::tls_client};
    websocket::stream<net::ssl::stream<tcp::socket>> stream{io, tls};
    beast::flat_buffer buffer;

    Impl() {
        tls.set_default_verify_paths();
        stream.read_message_max(1024 * 1024);
    }

    template <typename Start>
    void run(std::string_view stage, std::chrono::milliseconds timeout, Start start) {
        with_deadline(io, timeout, stage, start, [this] {
            boost::system::error_code ignored;
            stream.next_layer().next_layer().close(ignored);
        });
    }
};

KrakenWebSocketClient::KrakenWebSocketClient(std::string host, std::string port)
    : host_(std::move(host)), port_(std::move(port)), impl_(std::make_unique<Impl>()) {}

KrakenWebSocketClient::~KrakenWebSocketClient() = default;

void KrakenWebSocketClient::connect() {
    auto& ws = impl_->stream;
    tcp::resolver resolver(impl_->io);
    tcp::resolver::results_type endpoints;
    with_deadline(
        impl_->io, OPERATION_TIMEOUT, "DNS lookup",
        [&](auto done) {
            resolver.async_resolve(host_, port_, [&, done](auto error, auto results) {
                endpoints = std::move(results);
                done(error);
            });
        },
        [&] { resolver.cancel(); });

    ws.next_layer().set_verify_mode(net::ssl::verify_peer);
    ws.next_layer().set_verify_callback(net::ssl::host_name_verification(host_));
    if (SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host_.c_str()) != 1) {
        throw ConnectionError("Failed to set TLS SNI");
    }

    impl_->run("TCP connect", OPERATION_TIMEOUT, [&](auto done) {
        net::async_connect(ws.next_layer().next_layer(), endpoints, done);
    });
    impl_->run("TLS handshake", OPERATION_TIMEOUT, [&](auto done) {
        ws.next_layer().async_handshake(net::ssl::stream_base::client, done);
    });
    impl_->run("WebSocket handshake", OPERATION_TIMEOUT,
               [&](auto done) { ws.async_handshake(host_, "/v2", done); });
}

void KrakenWebSocketClient::subscribe_book(const std::string& symbol, std::size_t depth) {
    const json subscription = {
        {"method", "subscribe"},
        {"params",
         {{"channel", "book"}, {"symbol", {symbol}}, {"depth", depth}, {"snapshot", true}}}};

    const auto message = subscription.dump();
    impl_->run("Book subscription", OPERATION_TIMEOUT,
               [&](auto done) { impl_->stream.async_write(net::buffer(message), done); });
}

std::string KrakenWebSocketClient::read_message(std::chrono::milliseconds timeout) {
    impl_->run("WebSocket read", timeout,
               [&](auto done) { impl_->stream.async_read(impl_->buffer, done); });
    if (!impl_->stream.got_text()) {
        throw ConnectionError("Expected a text WebSocket message");
    }
    auto message = beast::buffers_to_string(impl_->buffer.data());
    impl_->buffer.consume(impl_->buffer.size());
    return message;
}

void KrakenWebSocketClient::close() {
    if (impl_->stream.is_open()) {
        impl_->run("WebSocket close", OPERATION_TIMEOUT, [&](auto done) {
            impl_->stream.async_close(websocket::close_code::normal, done);
        });
    }
}

const std::string& KrakenWebSocketClient::host() const noexcept { return host_; }

const std::string& KrakenWebSocketClient::port() const noexcept { return port_; }

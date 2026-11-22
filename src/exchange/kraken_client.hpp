#pragma once

#include "live_feed.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

class KrakenWebSocketClient final : public LiveConnection {
  public:
    KrakenWebSocketClient(std::string host, std::string port);
    ~KrakenWebSocketClient() override;

    void connect();
    void subscribe_book(const std::string& symbol, std::size_t depth);

    std::string read_message(std::chrono::milliseconds timeout = std::chrono::seconds(10)) override;
    void close();

    const std::string& host() const noexcept;
    const std::string& port() const noexcept;

  private:
    struct Impl;

    std::string host_;
    std::string port_;
    std::unique_ptr<Impl> impl_;
};

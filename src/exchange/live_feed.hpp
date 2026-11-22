#pragma once

#include "market_data/book_feed.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <string>

class ConnectionError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class LiveConnection {
  public:
    virtual ~LiveConnection() = default;
    virtual std::string read_message(std::chrono::milliseconds timeout) = 0;
};

using OpenConnection = std::function<std::unique_ptr<LiveConnection>()>;
using OnBookUpdate = std::function<void(const BookUpdate&, std::size_t received)>;
using OnReconnect = std::function<bool(const char* reason, std::chrono::milliseconds delay)>;

void run_live_feed(const std::string& symbol, const OpenConnection& open,
                   const OnBookUpdate& update, const OnReconnect& reconnect,
                   std::ostream* capture = nullptr);

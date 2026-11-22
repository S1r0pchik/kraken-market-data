#pragma once

#include "kraken_parser.hpp"

#include <optional>
#include <stdexcept>
#include <string>

class FeedError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct Quote {
    double bid;
    double ask;

    bool operator==(const Quote&) const = default;
};

struct BookUpdate {
    BookMessageType type;
    std::optional<Quote> changed_quote;
};

class BookFeed {
  public:
    explicit BookFeed(std::string symbol);

    void reset() noexcept;
    std::optional<BookUpdate> process(const std::string& frame);

  private:
    std::string symbol_;
    KrakenBookParser parser_;
    OrderBook book_;
    std::optional<Quote> last_quote_;
};

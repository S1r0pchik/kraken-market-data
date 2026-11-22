#pragma once

#include "../core/order_book.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class BookMessageType { Snapshot, Update };

struct BookMessage {
    BookMessageType type;
    std::string symbol;

    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;

    std::uint32_t checksum;
};

class KrakenBookParser {
  public:
    [[nodiscard]]
    std::optional<BookMessage> parse(const std::string& message) const;
};

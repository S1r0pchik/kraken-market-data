
#pragma once

#include "../core/order_book.hpp"

#include <cstdint>
#include <vector>

class KrakenBookChecksum {
  public:
    [[nodiscard]]
    static std::uint32_t calculate(const std::vector<PriceLevel>& bids,
                                   const std::vector<PriceLevel>& asks);

    static void verify(const OrderBook& book, std::uint32_t expected);
};

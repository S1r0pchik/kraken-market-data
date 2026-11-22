
#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct PriceLevel {
    double price;
    double qty;

    std::string price_text;
    std::string qty_text;
};

class OrderBook {
  public:
    static constexpr std::size_t DEPTH = 10;

    void apply_snapshot(std::vector<PriceLevel> bids, std::vector<PriceLevel> asks);

    void apply_update(const std::vector<PriceLevel>& bid_updates,
                      const std::vector<PriceLevel>& ask_updates);

    double best_bid() const;
    double best_ask() const;

    const std::vector<PriceLevel>& bids() const noexcept;
    const std::vector<PriceLevel>& asks() const noexcept;

  private:
    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;

    bool initialized_ = false;

    void trim();
    void validate() const;

    std::pair<std::size_t, bool> find_bid(double price);
    std::pair<std::size_t, bool> find_ask(double price);

    void update_bid(const PriceLevel& level);
    void update_ask(const PriceLevel& level);
};

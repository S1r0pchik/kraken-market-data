
#include "order_book.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

void OrderBook::apply_snapshot(std::vector<PriceLevel> bids, std::vector<PriceLevel> asks) {
    std::sort(bids.begin(), bids.end(),
              [](const PriceLevel& a, const PriceLevel& b) { return a.price > b.price; });

    std::sort(asks.begin(), asks.end(),
              [](const PriceLevel& a, const PriceLevel& b) { return a.price < b.price; });

    bids_ = std::move(bids);
    asks_ = std::move(asks);

    trim();
    validate();

    initialized_ = true;
}

std::pair<std::size_t, bool> OrderBook::find_bid(double price) {
    std::size_t left = 0;
    std::size_t right = bids_.size();

    while (left < right) {
        std::size_t mid = left + (right - left) / 2;

        if (bids_[mid].price == price) {
            return {mid, true};
        } else if (bids_[mid].price < price) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }

    return {left, false};
}

std::pair<std::size_t, bool> OrderBook::find_ask(double price) {
    std::size_t left = 0;
    std::size_t right = asks_.size();

    while (left < right) {
        std::size_t mid = left + (right - left) / 2;

        if (asks_[mid].price == price) {
            return {mid, true};
        } else if (asks_[mid].price > price) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }

    return {left, false};
}

void OrderBook::update_bid(const PriceLevel& level) {
    auto [index, found] = find_bid(level.price);

    if (found) {
        if (level.qty == 0.0) {
            bids_.erase(bids_.begin() + index);
        } else {
            bids_[index] = level;
        }
    } else if (level.qty > 0.0) {
        bids_.insert(bids_.begin() + index, level);
    }
}

void OrderBook::update_ask(const PriceLevel& level) {
    auto [index, found] = find_ask(level.price);

    if (found) {
        if (level.qty == 0.0) {
            asks_.erase(asks_.begin() + index);
        } else {
            asks_[index] = level;
        }
    } else if (level.qty > 0.0) {
        asks_.insert(asks_.begin() + index, level);
    }
}

void OrderBook::apply_update(const std::vector<PriceLevel>& bid_updates,
                             const std::vector<PriceLevel>& ask_updates) {
    if (!initialized_) {
        throw std::runtime_error("Update before snapshot");
    }

    for (const auto& level : bid_updates) {
        update_bid(level);
    }

    for (const auto& level : ask_updates) {
        update_ask(level);
    }

    trim();
    validate();
}

double OrderBook::best_bid() const {
    if (bids_.empty()) {
        throw std::runtime_error("Bids empty");
    }
    return bids_.front().price;
}

double OrderBook::best_ask() const {
    if (asks_.empty()) {
        throw std::runtime_error("Asks empty");
    }
    return asks_.front().price;
}

const std::vector<PriceLevel>& OrderBook::bids() const noexcept { return bids_; }

const std::vector<PriceLevel>& OrderBook::asks() const noexcept { return asks_; }

void OrderBook::trim() {
    if (bids_.size() > DEPTH) {
        bids_.resize(DEPTH);
    }
    if (asks_.size() > DEPTH) {
        asks_.resize(DEPTH);
    }
}

void OrderBook::validate() const {
    if (bids_.empty() || asks_.empty()) {
        throw std::runtime_error("Empty order book");
    }

    for (std::size_t i = 1; i < bids_.size(); ++i) {
        if (bids_[i - 1].price <= bids_[i].price) {
            throw std::runtime_error("Bids not sorted");
        }
    }

    for (std::size_t i = 1; i < asks_.size(); ++i) {
        if (asks_[i - 1].price >= asks_[i].price) {
            throw std::runtime_error("Asks not sorted");
        }
    }

    if (bids_.front().price >= asks_.front().price) {
        throw std::runtime_error("Invalid bid/ask spread");
    }
}

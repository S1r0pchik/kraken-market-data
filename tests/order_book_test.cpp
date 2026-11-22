#include "core/order_book.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {

PriceLevel level(double price, double qty = 1.0) { return {price, qty, {}, {}}; }

TEST(OrderBookTest, SortsSnapshotWithBestPricesFirst) {
    OrderBook book;
    book.apply_snapshot({level(99), level(98), level(100)}, {level(103), level(102), level(101)});

    ASSERT_EQ(book.bids().size(), 3U);
    EXPECT_EQ(book.best_bid(), 100.0);
    EXPECT_EQ(book.bids()[1].price, 99.0);
    EXPECT_EQ(book.bids()[2].price, 98.0);

    ASSERT_EQ(book.asks().size(), 3U);
    EXPECT_EQ(book.best_ask(), 101.0);
    EXPECT_EQ(book.asks()[1].price, 102.0);
    EXPECT_EQ(book.asks()[2].price, 103.0);
}

TEST(OrderBookTest, InsertsReplacesAndRemovesPriceLevels) {
    OrderBook book;
    book.apply_snapshot({level(100), level(99)}, {level(102), level(103)});

    book.apply_update({level(100, 0), level(101, 5), {99.0, 3.5, "99.0", "3.500"}},
                      {level(102, 0), level(104, 0), level(103, 2), level(105, 4)});

    ASSERT_EQ(book.bids().size(), 2U);
    EXPECT_EQ(book.best_bid(), 101.0);
    EXPECT_EQ(book.bids().front().qty, 5.0);
    EXPECT_EQ(book.bids().back().price, 99.0);
    EXPECT_EQ(book.bids().back().qty, 3.5);
    EXPECT_EQ(book.bids().back().qty_text, "3.500");

    ASSERT_EQ(book.asks().size(), 2U);
    EXPECT_EQ(book.best_ask(), 103.0);
    EXPECT_EQ(book.asks().front().qty, 2.0);
    EXPECT_EQ(book.asks().back().price, 105.0);
}

TEST(OrderBookTest, KeepsTenBestLevelsAfterSnapshotsAndUpdates) {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    for (int i = 0; i < 12; ++i) {
        bids.push_back(level(90 + i));
        asks.push_back(level(120 - i));
    }

    OrderBook book;
    book.apply_snapshot(bids, asks);

    ASSERT_EQ(book.bids().size(), 10U);
    EXPECT_EQ(book.bids().back().price, 92.0);
    ASSERT_EQ(book.asks().size(), 10U);
    EXPECT_EQ(book.asks().back().price, 118.0);

    book.apply_update({level(102)}, {level(108)});

    ASSERT_EQ(book.bids().size(), 10U);
    EXPECT_EQ(book.best_bid(), 102.0);
    EXPECT_EQ(book.bids().back().price, 93.0);
    ASSERT_EQ(book.asks().size(), 10U);
    EXPECT_EQ(book.best_ask(), 108.0);
    EXPECT_EQ(book.asks().back().price, 117.0);
}

TEST(OrderBookTest, RejectsUpdatesBeforeSnapshot) {
    OrderBook book;

    EXPECT_THROW(book.apply_update({level(100)}, {level(101)}), std::runtime_error);
}

TEST(OrderBookTest, RejectsCrossedBook) {
    OrderBook book;

    EXPECT_THROW(book.apply_snapshot({level(102)}, {level(101)}), std::runtime_error);
}

} // namespace

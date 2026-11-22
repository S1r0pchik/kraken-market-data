#include "market_data/kraken_checksum.hpp"
#include "market_data/kraken_parser.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace {

TEST(KrakenBookChecksumTest, HashesAsksBeforeBidsAndPreservesDecimalPrecision) {
    const std::vector<PriceLevel> bids{
        {99.5, 0.000123, "99.50", "1.2300e-4"},
        {99.0, 2.0, "99", "2"},
    };
    const std::vector<PriceLevel> asks{
        {100.01, 0.0025, "100.0100", "0.002500"},
        {101.0, 3.0, "101", "3.00"},
    };

    EXPECT_EQ(KrakenBookChecksum::calculate(bids, asks), 893936434U);
}

TEST(KrakenBookChecksumTest, UsesOnlyTheTopTenLevelsOnEachSide) {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    for (int i = 0; i < 10; ++i) {
        const int bid = 100 - i;
        const int ask = 101 + i;
        bids.push_back({static_cast<double>(bid), 1.0, std::to_string(bid), "1"});
        asks.push_back({static_cast<double>(ask), 1.0, std::to_string(ask), "1"});
    }
    const auto expected = KrakenBookChecksum::calculate(bids, asks);

    bids.push_back({90.0, 2.0, "90", "2"});
    asks.push_back({111.0, 3.0, "111", "3"});

    EXPECT_EQ(KrakenBookChecksum::calculate(bids, asks), expected);
}

TEST(KrakenBookChecksumTest, VerifiesSnapshotAndUpdate) {
    const KrakenBookParser parser;
    const auto snapshot = parser.parse(R"({
        "channel": "book",
        "type": "snapshot",
        "data": [{
            "symbol": "BTC/USD",
            "bids": [{"price": 100, "qty": 1}],
            "asks": [{"price": 101, "qty": 1}],
            "checksum": 891625595
        }]
    })");
    ASSERT_TRUE(snapshot.has_value());

    OrderBook book;
    book.apply_snapshot(snapshot->bids, snapshot->asks);
    EXPECT_NO_THROW(KrakenBookChecksum::verify(book, snapshot->checksum));

    const auto update = parser.parse(R"({
        "channel": "book",
        "type": "update",
        "data": [{
            "symbol": "BTC/USD",
            "asks": [{"price": 101, "qty": 0}, {"price": 102, "qty": 2}],
            "checksum": 4094765061
        }]
    })");
    ASSERT_TRUE(update.has_value());

    book.apply_update(update->bids, update->asks);
    EXPECT_NO_THROW(KrakenBookChecksum::verify(book, update->checksum));
    EXPECT_THROW(KrakenBookChecksum::verify(book, snapshot->checksum), std::runtime_error);
}

} // namespace

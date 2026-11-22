#include "market_data/kraken_parser.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {

TEST(KrakenBookParserTest, ReadsSnapshotAndPreservesDecimalText) {
    const auto message = KrakenBookParser{}.parse(R"({
        "channel": "book",
        "type": "snapshot",
        "data": [{
            "symbol": "BTC/USD",
            "bids": [{"price": 100.0100, "qty": 1.2300e-4}],
            "asks": [{"price": 101, "qty": 2.5000}],
            "checksum": 586518982
        }]
    })");

    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->type, BookMessageType::Snapshot);
    EXPECT_EQ(message->symbol, "BTC/USD");
    EXPECT_EQ(message->checksum, 586518982U);

    ASSERT_EQ(message->bids.size(), 1U);
    EXPECT_DOUBLE_EQ(message->bids.front().price, 100.01);
    EXPECT_DOUBLE_EQ(message->bids.front().qty, 0.000123);
    EXPECT_EQ(message->bids.front().price_text, "100.0100");
    EXPECT_EQ(message->bids.front().qty_text, "1.2300e-4");

    ASSERT_EQ(message->asks.size(), 1U);
    EXPECT_DOUBLE_EQ(message->asks.front().price, 101.0);
    EXPECT_DOUBLE_EQ(message->asks.front().qty, 2.5);
    EXPECT_EQ(message->asks.front().price_text, "101");
    EXPECT_EQ(message->asks.front().qty_text, "2.5000");
}

TEST(KrakenBookParserTest, ReadsOneSidedUpdateWithZeroQuantity) {
    const auto message = KrakenBookParser{}.parse(R"({
        "channel": "book",
        "type": "update",
        "data": [{
            "symbol": "BTC/USD",
            "bids": [{"price": 100, "qty": 0}],
            "checksum": 123
        }]
    })");

    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->type, BookMessageType::Update);
    EXPECT_TRUE(message->asks.empty());
    ASSERT_EQ(message->bids.size(), 1U);
    EXPECT_DOUBLE_EQ(message->bids.front().qty, 0.0);
    EXPECT_EQ(message->bids.front().qty_text, "0");
}

TEST(KrakenBookParserTest, IgnoresHeartbeatAndSuccessfulSubscription) {
    const KrakenBookParser parser;

    EXPECT_FALSE(parser.parse(R"({"channel": "heartbeat"})").has_value());
    EXPECT_FALSE(parser.parse(R"({"method": "subscribe", "success": true})").has_value());
}

TEST(KrakenBookParserTest, RejectsFailedSubscription) {
    const KrakenBookParser parser;

    EXPECT_THROW((void)parser.parse(R"({"method": "subscribe", "success": false})"),
                 std::runtime_error);
}

TEST(KrakenBookParserTest, RejectsSnapshotWithoutBothSides) {
    const KrakenBookParser parser;

    EXPECT_THROW((void)parser.parse(R"({
        "channel": "book",
        "type": "snapshot",
        "data": [{"symbol": "BTC/USD", "bids": [], "checksum": 123}]
    })"),
                 std::runtime_error);
}

TEST(KrakenBookParserTest, RejectsPriceLevelWithoutNumericQuantity) {
    const KrakenBookParser parser;

    EXPECT_THROW((void)parser.parse(R"({
        "channel": "book",
        "type": "update",
        "data": [{
            "symbol": "BTC/USD",
            "asks": [{"price": 101, "qty": "2"}],
            "checksum": 123
        }]
    })"),
                 std::runtime_error);
}

} // namespace

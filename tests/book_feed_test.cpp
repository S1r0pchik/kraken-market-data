#include "exchange/live_feed.hpp"
#include "market_data/book_feed.hpp"
#include "market_data/capture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <sstream>
#include <string>

namespace {

using namespace std::chrono_literals;

constexpr auto SNAPSHOT = R"({
    "channel":"book","type":"snapshot","data":[{
        "symbol":"BTC/USD","bids":[{"price":100,"qty":1}],
        "asks":[{"price":101,"qty":1}],"checksum":891625595
    }]
})";

constexpr auto UPDATE = R"({
    "channel":"book","type":"update","data":[{
        "symbol":"BTC/USD","asks":[{"price":101,"qty":0},{"price":102,"qty":2}],
        "checksum":4094765061
    }]
})";

constexpr auto SNAPSHOT_AT_102 = R"({
    "channel":"book","type":"snapshot","data":[{
        "symbol":"BTC/USD","bids":[{"price":100,"qty":1}],
        "asks":[{"price":102,"qty":2}],"checksum":4094765061
    }]
})";

constexpr auto BAD_UPDATE = R"({
    "channel":"book","type":"update","data":[{
        "symbol":"BTC/USD","bids":[{"price":101,"qty":3}],"checksum":0
    }]
})";

TEST(BookFeedTest, WaitsForVerifiedSnapshotAndDropsBookOnChecksumMismatch) {
    BookFeed feed("BTC/USD");
    EXPECT_FALSE(feed.process(UPDATE));

    const auto first = feed.process(SNAPSHOT);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->type, BookMessageType::Snapshot);
    EXPECT_EQ(first->changed_quote, (Quote{100.0, 101.0}));

    EXPECT_THROW(feed.process(BAD_UPDATE), FeedError);
    EXPECT_FALSE(feed.process(UPDATE));

    const auto fresh = feed.process(SNAPSHOT_AT_102);
    ASSERT_TRUE(fresh);
    EXPECT_EQ(fresh->changed_quote, (Quote{100.0, 102.0}));
}

TEST(BookFeedTest, PublishesSameQuoteAgainOnlyAfterReset) {
    BookFeed feed("BTC/USD");
    ASSERT_TRUE(feed.process(SNAPSHOT));

    const auto unchanged = feed.process(SNAPSHOT);
    ASSERT_TRUE(unchanged);
    EXPECT_FALSE(unchanged->changed_quote);

    feed.reset();
    EXPECT_FALSE(feed.process(UPDATE));
    const auto reconnected = feed.process(SNAPSHOT);
    ASSERT_TRUE(reconnected);
    EXPECT_EQ(reconnected->changed_quote, (Quote{100.0, 101.0}));
}

TEST(BookFeedTest, RejectsInvalidJsonAndWrongSymbol) {
    BookFeed feed("BTC/USD");
    ASSERT_TRUE(feed.process(SNAPSHOT));
    EXPECT_THROW(feed.process("{"), FeedError);
    EXPECT_FALSE(feed.process(UPDATE));

    std::string wrong_symbol(SNAPSHOT);
    wrong_symbol.replace(wrong_symbol.find("BTC/USD"), 7, "ETH/USD");
    EXPECT_THROW(feed.process(wrong_symbol), FeedError);
    EXPECT_FALSE(feed.process(UPDATE));
}

TEST(BookFeedTest, RejectsSnapshotWithChecksumOutsideUint32) {
    BookFeed feed("BTC/USD");
    std::string wrapped_crc(SNAPSHOT);
    wrapped_crc.replace(wrapped_crc.find("891625595"), 9, "5186592891");

    EXPECT_THROW(feed.process(wrapped_crc), FeedError);
    EXPECT_FALSE(feed.process(UPDATE));
    EXPECT_TRUE(feed.process(SNAPSHOT));
}

TEST(BookFeedTest, RejectsMalformedLevelEvenWithMatchingChecksum) {
    BookFeed feed("BTC/USD");
    std::string invalid_level(SNAPSHOT);
    invalid_level.insert(invalid_level.find("\"asks\":[") + 8, "null,");

    EXPECT_THROW(feed.process(invalid_level), FeedError);
    EXPECT_FALSE(feed.process(UPDATE));
    EXPECT_TRUE(feed.process(SNAPSHOT));
}

TEST(CaptureTest, PreservesExactFrameAcrossRoundTrip) {
    const std::string raw = "{\n  \"price\": 100.0100, \"qty\": 1.2300e-4\n}";
    std::stringstream capture;
    record_session(capture);
    record_frame(capture, raw);

    CaptureReader reader(capture);
    const auto session = reader.next();
    ASSERT_TRUE(session);
    EXPECT_EQ(session->line, 1U);
    EXPECT_FALSE(session->frame);

    const auto frame = reader.next();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->line, 2U);
    EXPECT_EQ(frame->frame, raw);
    EXPECT_FALSE(reader.next());
}

TEST(CaptureTest, ReportsMalformedCaptureLine) {
    std::istringstream input("{\"event\":\"session\"}\n[");
    CaptureReader reader(input);
    ASSERT_TRUE(reader.next());

    try {
        reader.next();
        FAIL() << "Expected invalid capture line";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("line 2"), std::string::npos);
    }
}

TEST(CaptureTest, ResumesOnlyAtNextRecordedSessionAfterBadFrame) {
    ReplayFeed replay("BTC/USD");
    ASSERT_TRUE(replay.process({1, SNAPSHOT}));
    EXPECT_THROW(replay.process({2, BAD_UPDATE}), FeedError);
    EXPECT_FALSE(replay.process({3, SNAPSHOT_AT_102}));
    EXPECT_FALSE(replay.process({4, std::nullopt}));
    EXPECT_FALSE(replay.process({5, UPDATE}));

    const auto fresh = replay.process({6, SNAPSHOT_AT_102});
    ASSERT_TRUE(fresh);
    EXPECT_EQ(fresh->changed_quote, (Quote{100.0, 102.0}));
}

class ScriptedConnection final : public LiveConnection {
  public:
    explicit ScriptedConnection(std::vector<std::string> frames) : frames_(std::move(frames)) {}

    std::string read_message(std::chrono::milliseconds timeout) override {
        EXPECT_GT(timeout, 0ms);
        EXPECT_LE(timeout, 10s);
        if (next_ == frames_.size()) {
            throw ConnectionError("Simulated disconnect");
        }
        return frames_[next_++];
    }

  private:
    std::vector<std::string> frames_;
    std::size_t next_ = 0;
};

TEST(LiveFeedTest, ReconnectsAfterDisconnectAndCorruptionAndRecordsMatchingReplay) {
    const std::vector<std::vector<std::string>> sessions{
        {SNAPSHOT, UPDATE},
        {UPDATE, SNAPSHOT_AT_102, BAD_UPDATE, SNAPSHOT},
        {UPDATE, SNAPSHOT},
    };
    std::size_t opened = 0;
    std::vector<Quote> live_quotes;
    std::vector<std::size_t> received;
    std::vector<std::string> failures;
    std::vector<std::chrono::milliseconds> delays;
    std::stringstream capture;

    run_live_feed(
        "BTC/USD", [&] { return std::make_unique<ScriptedConnection>(sessions.at(opened++)); },
        [&](const BookUpdate& tick, std::size_t count) {
            if (tick.changed_quote) {
                live_quotes.push_back(*tick.changed_quote);
                received.push_back(count);
            }
        },
        [&](const char* reason, std::chrono::milliseconds delay) {
            failures.emplace_back(reason);
            delays.push_back(delay);
            return opened < sessions.size();
        },
        &capture);

    EXPECT_EQ(opened, 3U);
    ASSERT_EQ(failures.size(), 3U);
    EXPECT_EQ(failures[0], "Simulated disconnect");
    EXPECT_NE(failures[1].find("CRC32 mismatch"), std::string::npos);
    EXPECT_EQ(failures[2], "Simulated disconnect");
    EXPECT_EQ(delays, (std::vector<std::chrono::milliseconds>{1s, 2s, 4s}));
    EXPECT_EQ(received, (std::vector<std::size_t>{1, 2, 4, 7}));
    EXPECT_EQ(live_quotes, (std::vector<Quote>{{100, 101}, {100, 102}, {100, 102}, {100, 101}}));

    CaptureReader reader(capture);
    ReplayFeed replay("BTC/USD");
    std::vector<Quote> replay_quotes;
    std::size_t reconnects = 0;
    std::size_t rejected = 0;
    while (const auto entry = reader.next()) {
        if (!entry->frame) {
            ++reconnects;
        }
        try {
            if (const auto tick = replay.process(*entry); tick && tick->changed_quote) {
                replay_quotes.push_back(*tick->changed_quote);
            }
        } catch (const FeedError&) {
            ++rejected;
        }
    }

    EXPECT_EQ(reconnects, 3U);
    EXPECT_EQ(rejected, 1U);
    EXPECT_EQ(replay_quotes, live_quotes);
}

TEST(LiveFeedTest, CapsReconnectBackoffAcrossConnectionFailures) {
    std::vector<std::chrono::milliseconds> delays;
    run_live_feed(
        "BTC/USD",
        []() -> std::unique_ptr<LiveConnection> { throw ConnectionError("Connect failed"); },
        [](const BookUpdate&, std::size_t) { FAIL() << "Unexpected quote"; },
        [&](const char*, std::chrono::milliseconds delay) {
            delays.push_back(delay);
            return delays.size() < 9;
        });
    EXPECT_EQ(delays,
              (std::vector<std::chrono::milliseconds>{1s, 2s, 4s, 8s, 16s, 32s, 60s, 60s, 60s}));
}

} // namespace

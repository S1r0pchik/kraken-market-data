#include "live_feed.hpp"
#include "market_data/capture.hpp"

#include <algorithm>
#include <optional>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto FRAME_TIMEOUT = 10s;
constexpr auto SNAPSHOT_TIMEOUT = 15s;
constexpr auto HEALTHY_INTERVAL = 30s;
constexpr auto FIRST_RETRY = 1s;
constexpr auto MAX_RETRY = 60s;

std::chrono::milliseconds read_timeout(Clock::time_point snapshot_deadline, bool synced) {
    if (synced) {
        return FRAME_TIMEOUT;
    }

    const auto remaining =
        std::chrono::ceil<std::chrono::milliseconds>(snapshot_deadline - Clock::now());
    if (remaining <= 0ms) {
        throw ConnectionError("Timed out waiting for a verified book snapshot");
    }
    return std::min<std::chrono::milliseconds>(FRAME_TIMEOUT, remaining);
}

} // namespace

void run_live_feed(const std::string& symbol, const OpenConnection& open,
                   const OnBookUpdate& update, const OnReconnect& reconnect,
                   std::ostream* capture) {
    BookFeed feed(symbol);
    auto delay = FIRST_RETRY;
    std::size_t received = 0;

    for (;;) {
        std::optional<Clock::time_point> synced_at;
        std::string failure;
        try {
            auto connection = open();
            if (capture) {
                record_session(*capture);
            }
            const auto deadline = Clock::now() + SNAPSHOT_TIMEOUT;

            for (;;) {
                auto frame =
                    connection->read_message(read_timeout(deadline, synced_at.has_value()));
                ++received;
                if (capture) {
                    record_frame(*capture, frame);
                }
                if (const auto tick = feed.process(frame)) {
                    if (!synced_at) {
                        synced_at = Clock::now();
                    }
                    update(*tick, received);
                }
            }
        } catch (const ConnectionError& error) {
            failure = error.what();
        } catch (const FeedError& error) {
            failure = error.what();
        }

        feed.reset();
        if (synced_at && Clock::now() - *synced_at >= HEALTHY_INTERVAL) {
            delay = FIRST_RETRY;
        }
        if (!reconnect(failure.c_str(), delay)) {
            return;
        }
        delay = std::min(delay * 2, MAX_RETRY);
    }
}

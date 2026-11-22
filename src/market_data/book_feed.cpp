#include "book_feed.hpp"
#include "../../nlohmann/json.hpp"
#include "kraken_checksum.hpp"

#include <utility>

BookFeed::BookFeed(std::string symbol) : symbol_(std::move(symbol)) {}

void BookFeed::reset() noexcept {
    book_ = OrderBook{};
    last_quote_.reset();
}

std::optional<BookUpdate> BookFeed::process(const std::string& frame) {
    try {
        auto message = parser_.parse(frame);
        if (!message) {
            return std::nullopt;
        }
        if (message->symbol != symbol_) {
            throw std::runtime_error("Unexpected book symbol: " + message->symbol);
        }

        if (message->type == BookMessageType::Snapshot) {
            book_.apply_snapshot(std::move(message->bids), std::move(message->asks));
        } else {
            if (!last_quote_) {
                return std::nullopt;
            }
            book_.apply_update(message->bids, message->asks);
        }
        KrakenBookChecksum::verify(book_, message->checksum);

        const Quote quote{book_.best_bid(), book_.best_ask()};
        BookUpdate update{message->type, std::nullopt};
        if (last_quote_ != quote) {
            last_quote_ = quote;
            update.changed_quote = quote;
        }
        return update;
    } catch (const nlohmann::json::exception& error) {
        reset();
        throw FeedError(error.what());
    } catch (const std::runtime_error& error) {
        reset();
        throw FeedError(error.what());
    }
}

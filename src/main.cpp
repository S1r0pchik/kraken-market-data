#include "benchmark.hpp"
#include "exchange/kraken_client.hpp"
#include "market_data/capture.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

const std::string SYMBOL = "BTC/USD";
constexpr std::size_t DEFAULT_BENCHMARK_PASSES = 1000;

void print_quote(const Quote& quote) {
    std::cout << std::fixed << std::setprecision(2) << "Bid: " << quote.bid
              << " | Ask: " << quote.ask << " | Spread: " << quote.ask - quote.bid << '\n';
}

std::ifstream open_capture(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open capture: " + path.string());
    }
    return input;
}

std::ofstream open_capture_for_append(const std::filesystem::path& path) {
    if (std::filesystem::exists(path) && std::filesystem::file_size(path) != 0) {
        auto input = open_capture(path);
        input.seekg(-1, std::ios::end);
        if (input.get() != '\n') {
            throw std::runtime_error("Capture does not end at a line boundary: " + path.string());
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        throw std::runtime_error("Cannot open capture for writing: " + path.string());
    }
    return output;
}

void live(std::ostream* capture) {
    std::mt19937 random(std::random_device{}());
    std::size_t verified_updates = 0;

    run_live_feed(
        SYMBOL,
        [] {
            auto client = std::make_unique<KrakenWebSocketClient>("ws.kraken.com", "443");
            client->connect();
            client->subscribe_book(SYMBOL, OrderBook::DEPTH);
            return client;
        },
        [&](const BookUpdate& tick, std::size_t received) {
            if (tick.type == BookMessageType::Update && ++verified_updates % 100 == 0) {
                std::cerr << "Verified updates: " << verified_updates
                          << " | Received messages: " << received << '\n';
            }
            if (tick.changed_quote) {
                print_quote(*tick.changed_quote);
            }
        },
        [&](const char* reason, std::chrono::milliseconds delay) {
            std::uniform_int_distribution jitter(std::chrono::milliseconds::rep{0},
                                                 delay.count() / 10);
            const auto wait = delay - std::chrono::milliseconds{jitter(random)};
            std::cerr << reason << ". Reconnecting in " << wait.count() << " ms\n";
            std::this_thread::sleep_for(wait);
            return true;
        },
        capture);
}

void replay(std::istream& input) {
    CaptureReader reader(input);
    ReplayFeed feed(SYMBOL);
    std::size_t snapshots = 0;
    std::size_t updates = 0;
    std::size_t rejected = 0;

    while (auto entry = reader.next()) {
        try {
            if (const auto tick = feed.process(*entry)) {
                if (tick->type == BookMessageType::Snapshot) {
                    ++snapshots;
                } else {
                    ++updates;
                }
                if (tick->changed_quote) {
                    print_quote(*tick->changed_quote);
                }
            }
        } catch (const FeedError& error) {
            ++rejected;
            std::cerr << "Capture line " << entry->line << ": " << error.what() << '\n';
        }
    }

    if (snapshots == 0) {
        throw std::runtime_error("Capture contains no verified snapshot");
    }
    std::cerr << "Snapshots: " << snapshots << " | Updates: " << updates
              << " | Rejected sessions: " << rejected << '\n';
}

std::size_t parse_passes(std::string_view text) {
    std::size_t passes = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), passes);
    if (error != std::errc{} || end != text.data() + text.size() || passes == 0) {
        throw std::runtime_error("Benchmark passes must be a positive integer");
    }
    return passes;
}

void usage(std::ostream& output) {
    output << "Usage:\n"
              "  kraken_test\n"
              "  kraken_test --record FILE\n"
              "  kraken_test --replay FILE\n"
              "  kraken_test --benchmark FILE [PASSES]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 1) {
            live(nullptr);
            return 0;
        }

        const std::string_view mode = argv[1];
        if (mode == "--help" && argc == 2) {
            usage(std::cout);
        } else if (mode == "--record" && argc == 3) {
            auto output = open_capture_for_append(argv[2]);
            live(&output);
        } else if (mode == "--replay" && argc == 3) {
            auto input = open_capture(argv[2]);
            replay(input);
        } else if (mode == "--benchmark" && (argc == 3 || argc == 4)) {
            const auto passes = argc == 4 ? parse_passes(argv[3]) : DEFAULT_BENCHMARK_PASSES;
            auto input = open_capture(argv[2]);
            benchmark_capture(input, SYMBOL, passes, std::cout);
        } else {
            usage(std::cerr);
            return 2;
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

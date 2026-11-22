#include "benchmark.hpp"
#include "market_data/capture.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::duration<double, std::micro>;
constexpr std::size_t MAX_SAMPLES = 100'000;

struct PassCounts {
    std::size_t snapshots = 0;
    std::size_t updates = 0;
    std::size_t rejected_sessions = 0;

    std::size_t verified() const { return snapshots + updates; }
};

PassCounts run_pass(const std::vector<CaptureEntry>& entries, const std::string& symbol) {
    ReplayFeed replay(symbol);
    PassCounts counts;
    for (const auto& entry : entries) {
        try {
            if (const auto tick = replay.process(entry)) {
                if (tick->type == BookMessageType::Snapshot) {
                    ++counts.snapshots;
                } else {
                    ++counts.updates;
                }
            }
        } catch (const FeedError&) {
            ++counts.rejected_sessions;
        }
    }
    return counts;
}

std::vector<double> measure_latency(const std::vector<CaptureEntry>& entries,
                                    const std::string& symbol, std::size_t passes,
                                    std::size_t messages_per_pass) {
    std::vector<double> samples;
    samples.reserve(std::min(MAX_SAMPLES, messages_per_pass) * passes);

    for (std::size_t i = 0; i < passes; ++i) {
        ReplayFeed replay(symbol);
        for (const auto& entry : entries) {
            try {
                const auto start = Clock::now();
                const auto tick = replay.process(entry);
                const auto elapsed = Clock::now() - start;
                if (tick) {
                    samples.push_back(Microseconds(elapsed).count());
                    if (samples.size() == MAX_SAMPLES) {
                        return samples;
                    }
                }
            } catch (const FeedError&) {
                // A failed session cannot produce a latency sample.
            }
        }
    }
    return samples;
}

double percentile(const std::vector<double>& sorted, std::size_t percent) {
    const auto rank = (sorted.size() * percent + 99) / 100;
    return sorted[rank - 1];
}

} // namespace

void benchmark_capture(std::istream& input, const std::string& symbol, std::size_t passes,
                       std::ostream& output) {
    if (passes == 0) {
        throw std::invalid_argument("Benchmark passes must be a positive integer");
    }

    CaptureReader reader(input);
    std::vector<CaptureEntry> entries;
    while (auto entry = reader.next()) {
        entries.push_back(std::move(*entry));
    }

    const auto warmup = run_pass(entries, symbol);
    if (warmup.snapshots == 0) {
        throw std::runtime_error("Capture contains no verified snapshot");
    }

    double verified = 0;
    const auto start = Clock::now();
    for (std::size_t i = 0; i < passes; ++i) {
        verified += static_cast<double>(run_pass(entries, symbol).verified());
    }
    const auto elapsed = std::chrono::duration<double>(Clock::now() - start);

    const auto latency_passes =
        std::min(passes, std::max(std::size_t{1}, MAX_SAMPLES / warmup.verified()));
    auto latency = measure_latency(entries, symbol, latency_passes, warmup.verified());
    std::sort(latency.begin(), latency.end());

    output << "Passes: " << passes << " | Verified per pass: " << warmup.verified()
           << " (snapshots: " << warmup.snapshots << ", updates: " << warmup.updates << ")"
           << " | Rejected sessions per pass: " << warmup.rejected_sessions << '\n'
           << std::fixed << std::setprecision(2) << "Throughput: " << verified / elapsed.count()
           << " verified book messages/s\n"
           << "Latency (" << latency.size() << " samples): p50=" << percentile(latency, 50)
           << " us | p95=" << percentile(latency, 95) << " us | p99=" << percentile(latency, 99)
           << " us\n";
}

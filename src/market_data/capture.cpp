#include "capture.hpp"
#include "../../nlohmann/json.hpp"

#include <istream>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

using nlohmann::json;
constexpr std::string_view SESSION_MARKER = R"({"event":"session"})";

void write_line(std::ostream& output, std::string_view line) {
    output << line << '\n' << std::flush;
    if (!output) {
        throw std::runtime_error("Cannot write capture");
    }
}

} // namespace

void record_session(std::ostream& output) { write_line(output, SESSION_MARKER); }

void record_frame(std::ostream& output, const std::string& frame) {
    write_line(output, json(frame).dump());
}

CaptureReader::CaptureReader(std::istream& input) : input_(input) {}

std::optional<CaptureEntry> CaptureReader::next() {
    std::string line;
    if (!std::getline(input_, line)) {
        if (input_.bad() || !input_.eof()) {
            throw std::runtime_error("Cannot read capture after line " + std::to_string(line_));
        }
        return std::nullopt;
    }
    ++line_;

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line == SESSION_MARKER) {
        return CaptureEntry{line_, std::nullopt};
    }

    try {
        return CaptureEntry{line_, json::parse(line).get<std::string>()};
    } catch (const json::exception& error) {
        throw std::runtime_error("Invalid capture line " + std::to_string(line_) + ": " +
                                 error.what());
    }
}

ReplayFeed::ReplayFeed(std::string symbol) : feed_(std::move(symbol)) {}

std::optional<BookUpdate> ReplayFeed::process(const CaptureEntry& entry) {
    if (!entry.frame) {
        feed_.reset();
        session_rejected_ = false;
        return std::nullopt;
    }
    if (session_rejected_) {
        return std::nullopt;
    }

    try {
        return feed_.process(*entry.frame);
    } catch (const FeedError&) {
        session_rejected_ = true;
        throw;
    }
}

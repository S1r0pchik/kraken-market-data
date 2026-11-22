#pragma once

#include "book_feed.hpp"

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>

struct CaptureEntry {
    std::size_t line;
    std::optional<std::string> frame;
};

void record_session(std::ostream& output);
void record_frame(std::ostream& output, const std::string& frame);

class CaptureReader {
  public:
    explicit CaptureReader(std::istream& input);

    std::optional<CaptureEntry> next();

  private:
    std::istream& input_;
    std::size_t line_ = 0;
};

class ReplayFeed {
  public:
    explicit ReplayFeed(std::string symbol);

    std::optional<BookUpdate> process(const CaptureEntry& entry);

  private:
    BookFeed feed_;
    bool session_rejected_ = false;
};

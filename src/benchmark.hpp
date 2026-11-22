#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>

void benchmark_capture(std::istream& input, const std::string& symbol, std::size_t passes,
                       std::ostream& output);

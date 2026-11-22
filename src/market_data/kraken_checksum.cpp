#include "kraken_checksum.hpp"

#include <algorithm>
#include <boost/crc.hpp>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

std::string expand_decimal(std::string value) {
    auto exponent_pos = value.find_first_of("eE");

    if (exponent_pos == std::string::npos) {
        return value;
    }

    auto exponent_text = std::string_view(value).substr(exponent_pos + 1);
    if (exponent_text.starts_with('+')) {
        exponent_text.remove_prefix(1);
    }

    long exponent = 0;
    const auto end = exponent_text.data() + exponent_text.size();
    const auto [ptr, error] = std::from_chars(exponent_text.data(), end, exponent);
    if (error != std::errc{} || ptr != end || exponent < -1000 || exponent > 1000) {
        throw std::runtime_error("Invalid decimal exponent: " + value);
    }

    std::string mantissa = value.substr(0, exponent_pos);

    auto dot = mantissa.find('.');
    long decimal_position =
        (dot == std::string::npos) ? static_cast<long>(mantissa.size()) : static_cast<long>(dot);

    if (dot != std::string::npos) {
        mantissa.erase(dot, 1);
    }

    long new_position = decimal_position + exponent;
    long length = static_cast<long>(mantissa.size());

    if (new_position <= 0) {
        return "0." + std::string(static_cast<std::size_t>(-new_position), '0') + mantissa;
    }

    if (new_position >= length) {
        return mantissa + std::string(static_cast<std::size_t>(new_position - length), '0');
    }

    mantissa.insert(static_cast<std::size_t>(new_position), 1, '.');

    return mantissa;
}

std::string normalize_decimal(const std::string& value) {
    std::string decimal = expand_decimal(value);
    std::string result;
    bool skip_begin = true;

    for (char ch : decimal) {
        if (ch == '.') {
            continue;
        }

        if (ch < '0' || ch > '9') {
            throw std::runtime_error("Unsupported decimal: " + value);
        }

        if (ch == '0' && skip_begin) {
            continue;
        }

        result += ch;
        skip_begin = false;
    }

    return result.empty() ? "0" : result;
}

std::uint32_t calculate_crc32(const std::string& text) {
    boost::crc_32_type crc;
    crc.process_bytes(text.data(), text.size());
    return crc.checksum();
}

namespace {

constexpr std::size_t CHECKSUM_DEPTH = 10;

std::uint32_t calculate_book_crc32(const std::vector<PriceLevel>& bids,
                                   const std::vector<PriceLevel>& asks) {
    std::string input;

    for (std::size_t i = 0; i < std::min(CHECKSUM_DEPTH, asks.size()); ++i) {

        input += normalize_decimal(asks[i].price_text);
        input += normalize_decimal(asks[i].qty_text);
    }

    for (std::size_t i = 0; i < std::min(CHECKSUM_DEPTH, bids.size()); ++i) {

        input += normalize_decimal(bids[i].price_text);
        input += normalize_decimal(bids[i].qty_text);
    }

    return calculate_crc32(input);
}

} // namespace

std::uint32_t KrakenBookChecksum::calculate(const std::vector<PriceLevel>& bids,
                                            const std::vector<PriceLevel>& asks) {
    return calculate_book_crc32(bids, asks);
}

void KrakenBookChecksum::verify(const OrderBook& book, std::uint32_t expected) {
    const auto actual = calculate(book.bids(), book.asks());

    if (actual != expected) {
        throw std::runtime_error("CRC32 mismatch! Expected: " + std::to_string(expected) +
                                 ", actual: " + std::to_string(actual));
    }
}
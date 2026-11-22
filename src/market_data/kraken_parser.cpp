#include "kraken_parser.hpp"
#include "../../nlohmann/json.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

using json = nlohmann::json;

namespace {

enum class Container { Object, Array };

enum class Role { Other, Root, Data, BookEntry, Bids, Asks, BidLevel, AskLevel };

struct Frame {
    Frame(Container kind, Role role) : kind(kind), role(role) {}

    Container kind;
    Role role;

    std::string key;
    PriceLevel level{};

    bool has_price = false;
    bool has_qty = false;
};

class BookLevelSax final : public nlohmann::json_sax<json> {
  public:
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;

    bool start_object(std::size_t) override {
        Role role = Role::Other;

        if (stack_.empty()) {
            role = Role::Root;
        } else {
            const Frame& parent = stack_.back();

            if (parent.kind == Container::Array) {
                switch (parent.role) {
                case Role::Data:
                    role = Role::BookEntry;
                    break;
                case Role::Bids:
                    role = Role::BidLevel;
                    break;
                case Role::Asks:
                    role = Role::AskLevel;
                    break;
                default:
                    break;
                }
            }
        }

        stack_.emplace_back(Container::Object, role);
        return true;
    }

    bool end_object() override {
        if (stack_.empty() || stack_.back().kind != Container::Object) {
            return false;
        }

        Frame& frame = stack_.back();

        if (frame.role == Role::BidLevel || frame.role == Role::AskLevel) {

            if (!frame.has_price || !frame.has_qty) {
                return false;
            }

            if (frame.role == Role::BidLevel) {
                bids.push_back(std::move(frame.level));
            } else {
                asks.push_back(std::move(frame.level));
            }
        }

        stack_.pop_back();
        return true;
    }

    bool start_array(std::size_t) override {
        Role role = Role::Other;

        if (!stack_.empty()) {
            const Frame& parent = stack_.back();

            if (parent.kind == Container::Object) {
                if (parent.role == Role::Root && parent.key == "data") {
                    role = Role::Data;
                } else if (parent.role == Role::BookEntry) {
                    if (parent.key == "bids") {
                        role = Role::Bids;
                    } else if (parent.key == "asks") {
                        role = Role::Asks;
                    }
                }
            }
        }

        stack_.emplace_back(Container::Array, role);
        return true;
    }

    bool end_array() override {
        if (stack_.empty() || stack_.back().kind != Container::Array) {
            return false;
        }

        stack_.pop_back();
        return true;
    }

    bool key(string_t& key) override {
        if (stack_.empty() || stack_.back().kind != Container::Object) {
            return false;
        }

        stack_.back().key = key;
        return true;
    }

    bool number_float(number_float_t value, const string_t& original) override {
        return save_number(value, original);
    }

    bool number_integer(number_integer_t value) override {
        return save_number(static_cast<double>(value), std::to_string(value));
    }

    bool number_unsigned(number_unsigned_t value) override {
        return save_number(static_cast<double>(value), std::to_string(value));
    }

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool string(string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
        return false;
    }

  private:
    std::vector<Frame> stack_;

    bool save_number(double value, const std::string& original) {
        if (stack_.empty()) {
            return true;
        }

        Frame& frame = stack_.back();

        if (frame.role != Role::BidLevel && frame.role != Role::AskLevel) {
            return true;
        }

        if (frame.key == "price") {
            if (value <= 0.0) {
                return false;
            }
            frame.level.price = value;
            frame.level.price_text = original;
            frame.has_price = true;
        } else if (frame.key == "qty") {
            if (value < 0.0) {
                return false;
            }
            frame.level.qty = value;
            frame.level.qty_text = original;
            frame.has_qty = true;
        }

        return true;
    }
};

} // namespace

std::optional<BookMessage> KrakenBookParser::parse(const std::string& message) const {
    json root = json::parse(message);

    if (!root.is_object()) {
        throw std::runtime_error("Expected JSON object");
    }

    if (root.value("method", std::string{}) == "subscribe" && !root.value("success", true)) {
        throw std::runtime_error("Kraken subscription rejected: " + root.dump());
    }

    if (root.value("channel", std::string{}) != "book") {
        return std::nullopt;
    }

    const std::string type = root.at("type").get<std::string>();

    BookMessageType message_type;

    if (type == "snapshot") {
        message_type = BookMessageType::Snapshot;
    } else if (type == "update") {
        message_type = BookMessageType::Update;
    } else {
        throw std::runtime_error("Unknown book message type: " + type);
    }

    const json& data = root.at("data");

    if (!data.is_array() || data.size() != 1 || !data[0].is_object()) {
        throw std::runtime_error("Expected exactly one book entry");
    }

    const json& entry = data[0];

    for (const char* side : {"bids", "asks"}) {
        if (entry.contains(side) && !entry.at(side).is_array()) {
            throw std::runtime_error(std::string("Invalid book side: ") + side);
        }
    }

    if (message_type == BookMessageType::Snapshot &&
        (!entry.contains("bids") || !entry.contains("asks"))) {
        throw std::runtime_error("Snapshot is missing bids or asks");
    }

    const std::string symbol = entry.at("symbol").get<std::string>();

    const auto& checksum = entry.at("checksum");
    if (!checksum.is_number_unsigned() ||
        checksum.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Invalid book checksum");
    }

    BookLevelSax sax;

    if (!json::sax_parse(message, &sax) ||
        (entry.contains("bids") && sax.bids.size() != entry.at("bids").size()) ||
        (entry.contains("asks") && sax.asks.size() != entry.at("asks").size())) {
        throw std::runtime_error("Failed to parse book price levels");
    }

    return BookMessage{message_type, symbol, std::move(sax.bids), std::move(sax.asks),
                       checksum.get<std::uint32_t>()};
}

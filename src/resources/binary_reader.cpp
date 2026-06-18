#include "resources/binary_reader.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string hex_offset(std::size_t offset) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0') << std::setw(8) << offset;
    return output.str();
}

}  // namespace

namespace bumpy {

BinaryReader BinaryReader::from_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open resource: " + path.string());
    }

    std::vector<std::uint8_t> data;
    char byte{};
    while (input.get(byte)) {
        data.push_back(static_cast<std::uint8_t>(byte));
    }
    if (!input.eof()) {
        throw std::runtime_error("cannot read resource: " + path.string());
    }

    auto name = path.filename().string();
    if (name.empty()) {
        name = path.string();
    }
    return BinaryReader(std::move(name), std::move(data));
}

BinaryReader::BinaryReader(std::string resource_name, std::vector<std::uint8_t> data)
    : resource_name_(std::move(resource_name)), data_(std::move(data)) {}

std::uint8_t BinaryReader::u8() {
    require_available(1);
    return data_[offset_++];
}

std::uint16_t BinaryReader::u16_le() {
    require_available(2);
    const auto low = static_cast<std::uint16_t>(data_[offset_]);
    const auto high = static_cast<std::uint16_t>(data_[offset_ + 1]);
    offset_ += 2;
    return static_cast<std::uint16_t>(low | (high << 8));
}

std::int16_t BinaryReader::i16_le() {
    const auto value = u16_le();
    if (value < 0x8000) {
        return static_cast<std::int16_t>(value);
    }
    return static_cast<std::int16_t>(static_cast<int>(value) - 0x10000);
}

std::span<const std::uint8_t> BinaryReader::bytes(std::size_t count) {
    require_available(count);
    const auto start = offset_;
    offset_ += count;
    return std::span<const std::uint8_t>(data_.data() + start, count);
}

void BinaryReader::seek(std::size_t offset) {
    if (offset > data_.size()) {
        throw out_of_range_at(offset, 0);
    }
    offset_ = offset;
}

void BinaryReader::require_available(std::size_t count) const {
    if (count > data_.size() - offset_) {
        throw out_of_range_at(offset_, count);
    }
}

std::out_of_range BinaryReader::out_of_range_at(std::size_t offset, std::size_t count) const {
    std::ostringstream message;
    message << resource_name_ << ": read outside resource at " << hex_offset(offset)
            << " for " << count << " byte";
    if (count != 1) {
        message << "s";
    }
    return std::out_of_range(message.str());
}

}  // namespace bumpy

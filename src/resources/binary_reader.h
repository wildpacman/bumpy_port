#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace bumpy {

// Read an entire binary file into a byte vector (throws on open/read failure). For raw
// resources that are not VEC-compressed -- e.g. SCORE.VEC, a stored 320x200 screen image.
[[nodiscard]] std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path);

class BinaryReader {
public:
    static BinaryReader from_file(const std::filesystem::path& path);

    std::uint8_t u8();
    std::uint16_t u16_le();
    std::int16_t i16_le();
    std::span<const std::uint8_t> bytes(std::size_t count);
    void seek(std::size_t offset);
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

private:
    BinaryReader(std::string resource_name, std::vector<std::uint8_t> data);
    void require_available(std::size_t count) const;
    [[nodiscard]] std::out_of_range out_of_range_at(std::size_t offset, std::size_t count) const;

    std::string resource_name_;
    std::vector<std::uint8_t> data_;
    std::size_t offset_{};
};

}  // namespace bumpy

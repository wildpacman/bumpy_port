#include "resources/menu_resources.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string resource_name_from_path(const std::filesystem::path& path) {
    auto name = path.filename().string();
    if (name.empty()) {
        name = path.string();
    }
    return name;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
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
    return data;
}

std::string hex_offset(std::size_t offset) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0') << std::setw(8) << offset;
    return output.str();
}

[[noreturn]] void fail_at(const std::string& resource_name, std::size_t offset, const std::string& message) {
    throw std::runtime_error(resource_name + ": " + message + " at " + hex_offset(offset));
}

void require_range(
    std::span<const std::uint8_t> bytes,
    const std::string& resource_name,
    std::size_t offset,
    std::size_t count) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        fail_at(resource_name, offset, "read outside resource");
    }
}

std::uint32_t read_be_u32(std::span<const std::uint8_t> bytes, const std::string& resource_name, std::size_t offset) {
    require_range(bytes, resource_name, offset, 4);
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

}  // namespace

namespace bumpy {

SpriteArchive::SpriteArchive(
    std::vector<std::uint8_t> bytes,
    std::array<std::uint32_t, 3> root_offsets,
    std::vector<SpriteGroupTable> group_tables,
    std::vector<SpriteChildBlock> child_blocks)
    : bytes_(std::move(bytes)),
      root_offsets_(root_offsets),
      group_tables_(std::move(group_tables)),
      child_blocks_(std::move(child_blocks)) {}

std::span<const std::uint8_t> SpriteArchive::child(std::size_t index) const {
    if (index >= child_blocks_.size()) {
        throw std::out_of_range("sprite child index is outside archive");
    }
    const auto& block = child_blocks_[index];
    return std::span<const std::uint8_t>(bytes_.data() + block.start_offset, block.size);
}

SpriteArchive decode_sprite_archive(const std::filesystem::path& path) {
    return decode_sprite_archive(read_file(path), resource_name_from_path(path));
}

SpriteArchive decode_sprite_archive(std::span<const std::uint8_t> bytes, std::string resource_name) {
    std::array<std::uint32_t, 3> root_offsets{};
    for (std::size_t index = 0; index < root_offsets.size(); ++index) {
        root_offsets[index] = read_be_u32(bytes, resource_name, index * 4U);
    }
    if (root_offsets != std::array<std::uint32_t, 3>{12, 144, 276}) {
        fail_at(resource_name, 0, "sprite archive root offsets differ from confirmed layout");
    }

    std::vector<SpriteGroupTable> group_tables;
    group_tables.reserve(root_offsets.size());
    std::vector<std::size_t> child_offsets;
    child_offsets.reserve(99);
    for (std::size_t group_index = 0; group_index < root_offsets.size(); ++group_index) {
        const auto table_offset = static_cast<std::size_t>(root_offsets[group_index]);
        std::vector<std::size_t> entries;
        entries.reserve(33);
        for (std::size_t entry_index = 0; entry_index < 33; ++entry_index) {
            const auto entry_offset = table_offset + entry_index * 4U;
            entries.push_back(read_be_u32(bytes, resource_name, entry_offset));
        }
        if (!std::is_sorted(entries.begin(), entries.end())) {
            fail_at(resource_name, table_offset, "sprite archive group offsets are decreasing");
        }
        if (entries.front() < 408 || entries.back() >= bytes.size()) {
            fail_at(resource_name, table_offset, "sprite archive group offsets are outside the file");
        }
        child_offsets.insert(child_offsets.end(), entries.begin(), entries.end());
        group_tables.push_back(
            SpriteGroupTable{
                group_index,
                table_offset,
                entries.size(),
                entries.front(),
                entries.back(),
            });
    }

    const std::set<std::size_t> unique_offsets(child_offsets.begin(), child_offsets.end());
    if (unique_offsets.size() != child_offsets.size() || !std::is_sorted(child_offsets.begin(), child_offsets.end())) {
        fail_at(resource_name, 12, "sprite archive child offsets are not globally increasing");
    }
    if (child_offsets.front() != 408) {
        fail_at(resource_name, 12, "sprite archive has a gap between index and child blocks");
    }

    std::vector<SpriteChildBlock> child_blocks;
    child_blocks.reserve(child_offsets.size());
    for (std::size_t index = 0; index < child_offsets.size(); ++index) {
        const auto start_offset = child_offsets[index];
        const auto end_offset = index + 1 < child_offsets.size() ? child_offsets[index + 1] : bytes.size();
        if (end_offset < start_offset) {
            fail_at(resource_name, start_offset, "sprite archive child block has negative length");
        }
        child_blocks.push_back(
            SpriteChildBlock{
                index,
                index / 33U,
                index % 33U,
                start_offset,
                end_offset,
                end_offset - start_offset,
            });
    }

    return SpriteArchive(
        std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
        root_offsets,
        std::move(group_tables),
        std::move(child_blocks));
}

MenuResources MenuResources::load_from(const std::filesystem::path& root) {
    return MenuResources{
        decode_vec_resource(root / "TITRE.VEC"),
        decode_vec_resource(root / "BUMPRESE.VEC"),
        decode_vec_resource(root / "MASKBUMP.VEC"),
        decode_sprite_archive(root / "BUMSPJEU.BIN"),
        read_file(root / "FLECHE.BIN"),
    };
}

}  // namespace bumpy

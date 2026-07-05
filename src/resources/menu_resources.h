#pragma once

#include "resources/vec.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace bumpy {

struct SpriteGroupTable {
    std::size_t index{};
    std::size_t table_offset{};
    std::size_t entry_count{};
    std::size_t first_child_offset{};
    std::size_t last_child_offset{};
};

struct SpriteChildBlock {
    std::size_t index{};
    std::size_t group_index{};
    std::size_t entry_index{};
    std::size_t start_offset{};
    std::size_t end_offset{};
    std::size_t size{};
};

class SpriteArchive {
public:
    SpriteArchive(
        std::vector<std::uint8_t> bytes,
        std::array<std::uint32_t, 3> root_offsets,
        std::vector<SpriteGroupTable> group_tables,
        std::vector<SpriteChildBlock> child_blocks);

    [[nodiscard]] std::size_t terminal_offset() const noexcept { return bytes_.size(); }
    [[nodiscard]] const std::array<std::uint32_t, 3>& root_offsets() const noexcept { return root_offsets_; }
    [[nodiscard]] const std::vector<SpriteGroupTable>& group_tables() const noexcept { return group_tables_; }
    [[nodiscard]] const std::vector<SpriteChildBlock>& child_blocks() const noexcept { return child_blocks_; }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::span<const std::uint8_t> child(std::size_t index) const;

private:
    std::vector<std::uint8_t> bytes_;
    std::array<std::uint32_t, 3> root_offsets_{};
    std::vector<SpriteGroupTable> group_tables_;
    std::vector<SpriteChildBlock> child_blocks_;
};

struct MenuResources {
    VecResource title;
    VecResource splash;
    VecResource mask_bump;
    SpriteArchive bumper_sprites;
    std::vector<std::uint8_t> cursor_sprites;

    static MenuResources load_from(const std::filesystem::path& root);
};

SpriteArchive decode_sprite_archive(const std::filesystem::path& path);
SpriteArchive decode_sprite_archive(std::span<const std::uint8_t> bytes, std::string resource_name);

}  // namespace bumpy

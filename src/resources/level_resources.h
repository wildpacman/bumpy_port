#pragma once

#include "resources/vec.h"
#include "video/menu_renderer.h"  // MenuImage (deplaned object sheet)

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace bumpy {

// One static playfield board, decoded from a D?.DEC record. The decoded DEC is a
// 2-byte file header followed by N board records of 0x32c (812) bytes; each board
// is a 32-byte header then a 20-column x 13-row grid of 3-byte cells stored
// column-major (cell = base + 0x20 + col*0x27 + row*3). Recovered from the board
// renderer FUN_1000_2a0a / object blit FUN_1000_0a90; see
// analysis/specs/level-formats.md.
struct LevelBoard {
    static constexpr int columns = 20;
    static constexpr int rows = 13;
    static constexpr int cell_bytes = 3;
    static constexpr int column_stride = 0x27;        // 39 = rows * cell_bytes
    static constexpr int grid_offset = 0x20;          // cells follow the 32-byte board header
    static constexpr std::size_t record_size = 0x32c; // 812

    std::array<std::uint8_t, record_size> bytes{};

    // The three raw bytes of a cell; bytes[0] is the object index.
    [[nodiscard]] std::span<const std::uint8_t> cell(int col, int row) const;
    // Object index for a cell: 0 = empty, 1..0xf0 = single object, >=0xf1 = stacked.
    [[nodiscard]] std::uint8_t object_index(int col, int row) const;
};

// The decoded level blobs for one level number (D{n}.PAV/DEC/BUM). All three are
// layered-VEC containers decoded by the existing vec decoder, except D6.BUM and
// D9.BUM which ship raw (already decoded) and are detected by VEC-decode failure.
class LevelResources {
public:
    // PAV object sheet geometry (D?.PAV: 6-byte header + 320x192 plane-sequential
    // 4-plane VGA image). Tiles are 16x16, laid out 20 columns wide.
    static constexpr int sheet_width = 320;
    static constexpr int sheet_height = 192;
    static constexpr int sheet_header = 6;
    static constexpr int tile_size = 16;
    static constexpr int sheet_columns = 20;  // sheet_width / tile_size

    static LevelResources load(const std::filesystem::path& root, int level_number);

    [[nodiscard]] int level_number() const noexcept { return level_number_; }
    [[nodiscard]] std::size_t board_count() const noexcept { return boards_.size(); }
    [[nodiscard]] const LevelBoard& board(std::size_t index) const;

    // D3 ships a 0-byte PAV, so the object sheet is optional.
    [[nodiscard]] bool has_object_sheet() const noexcept { return object_sheet_.has_value(); }
    [[nodiscard]] const MenuImage& object_sheet() const;

    // Raw 194-byte D?.BUM board records (dynamic entities); not yet rendered.
    [[nodiscard]] std::span<const std::uint8_t> bum_board(std::size_t index) const;
    [[nodiscard]] bool bum_was_raw() const noexcept { return bum_was_raw_; }

private:
    int level_number_{};
    std::vector<LevelBoard> boards_;
    std::optional<MenuImage> object_sheet_;
    std::vector<std::uint8_t> bum_;  // decoded BUM, including the 2-byte file header
    std::size_t bum_board_count_{};
    bool bum_was_raw_{};
};

// Decode a 320x192 plane-sequential 4-plane VGA image (the D?.PAV body after its
// 6-byte header) into an indexed MenuImage. Colour index 0 is transparent.
MenuImage deplane_object_sheet(std::span<const std::uint8_t> pav_body);

}  // namespace bumpy

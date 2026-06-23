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

    static constexpr int palette_colors = 16;

    std::array<std::uint8_t, record_size> bytes{};

    // The three raw bytes of a cell; bytes[0] is the object index.
    [[nodiscard]] std::span<const std::uint8_t> cell(int col, int row) const;
    // Object index for a cell: 0 = empty, 1..0xf0 = single object, >=0xf1 = stacked.
    [[nodiscard]] std::uint8_t object_index(int col, int row) const;

    // The board's own in-level VGA palette: 16 RGB triplets of 6-bit DAC values
    // (r,g,b,r,g,b,...), the same layout as a screen's embedded 0x33 DAC palette.
    //
    // The 32-byte board header is NOT opaque -- it is 16 big-endian words, one per
    // colour. The original builds the palette in FUN_1000_063b (byteswap each word
    // into DS:0x578) and uploads it in FUN_1000_08d1's VGA branch, decoding every
    // word as R = high byte, G = bits 4..7, B = bits 0..3, each << 3 into a 6-bit
    // DAC register. This is per board (rebuilt on board entry via FUN_1000_0604) and
    // is the real gameplay palette -- the playfield does NOT inherit the brown MONDE
    // map palette. See analysis/specs/level-formats.md ("D?.DEC board palette").
    [[nodiscard]] std::array<std::uint8_t, palette_colors * 3> palette() const;
};

// One board's dynamic entities, decoded from a D?.BUM record (194 bytes). The
// record is three parallel 8-column x 6-row entity layers of 48 bytes each, then
// 6 board params at 0x90, then a 44-byte remainder. Recovered from the board
// activation FUN_1000_32b0 (which copies bytes 0x00..0x95 into the working buffer
// 203b:a0e4) and the spawn/draw routine FUN_1000_2a78 which iterates the grid as
// cell = row*8 + col and draws each non-zero cell through a per-layer path; see
// analysis/specs/level-formats.md ("D?.BUM entity layers").
struct BumEntities {
    static constexpr int columns = 8;
    static constexpr int rows = 6;
    static constexpr int cells = columns * rows;       // 48 = 0x30
    static constexpr std::size_t record_size = 0xc2;   // 194
    static constexpr std::size_t layer_a_offset = 0x00;  // peg/bumper grid (0/1)
    static constexpr std::size_t layer_b_offset = 0x30;  // second layer (col 7 unused)
    static constexpr std::size_t layer_c_offset = 0x60;  // collectibles; sprite = value + 0x179
    static constexpr std::size_t params_offset = 0x90;   // 6 board params
    static constexpr int param_count = 6;

    std::array<std::uint8_t, record_size> bytes{};

    // Cell value for each layer at (col,row); cell = row*8 + col. 0 = empty.
    // Layer A is the bumper/peg grid; layer C's value selects the collectible
    // sprite; layer B is a second entity layer (its col 7 is never drawn).
    [[nodiscard]] std::uint8_t layer_a(int col, int row) const;
    [[nodiscard]] std::uint8_t layer_b(int col, int row) const;
    [[nodiscard]] std::uint8_t layer_c(int col, int row) const;
    // Board params 0..5 (0x90..0x95). Params 0/1 are 1-based grid cell indices
    // (decremented when non-zero by the spawn routine); 2/4 are small counts.
    [[nodiscard]] std::uint8_t param(int index) const;
};

// Faithful screen position (sprite top-left) of a BUM grid cell, read from the
// data-segment coordinate table at DS:0x274 in BUMPY.UNPACKED.EXE (48 (x,y) word
// pairs): x = 8 + col*40 for all eight columns (col 7 is the rightmost, at x = 288)
// and rows at y = 8 + row*32. See analysis/specs/level-formats.md.
struct CellPosition {
    int x{};
    int y{};
};
[[nodiscard]] CellPosition bum_cell_position(int col, int row);

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

    // Raw 194-byte D?.BUM board records (dynamic entities).
    [[nodiscard]] std::span<const std::uint8_t> bum_board(std::size_t index) const;
    // The same record parsed into the three 8x6 entity layers + board params.
    [[nodiscard]] BumEntities bum_entities(std::size_t index) const;
    [[nodiscard]] std::size_t bum_board_count() const noexcept { return bum_board_count_; }
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

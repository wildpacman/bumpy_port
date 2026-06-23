#include <catch2/catch_test_macros.hpp>

#include "resources/level_resources.h"

#include <filesystem>

namespace {

// Tests run with the working directory at the project root, so the original
// D?.PAV/DEC/BUM files are reachable by name. Board counts and sizes below are
// the confirmed values from the decoded blobs (see analysis/specs/level-formats.md).
const std::filesystem::path root = ".";

}  // namespace

TEST_CASE("level 1 decodes a 15-board DEC, matching BUM, and a 320x192 object sheet") {
    const auto level = bumpy::LevelResources::load(root, 1);

    REQUIRE(level.level_number() == 1);
    REQUIRE(level.board_count() == 15);
    REQUIRE_FALSE(level.bum_was_raw());

    REQUIRE(level.has_object_sheet());
    REQUIRE(level.object_sheet().width == bumpy::LevelResources::sheet_width);
    REQUIRE(level.object_sheet().height == bumpy::LevelResources::sheet_height);
    REQUIRE(level.object_sheet().pixels.size() ==
            static_cast<std::size_t>(bumpy::LevelResources::sheet_width) *
                bumpy::LevelResources::sheet_height);

    // A playable board places at least one object somewhere on its 20x13 grid.
    const auto& board = level.board(0);
    bool any_object = false;
    for (int col = 0; col < bumpy::LevelBoard::columns; ++col) {
        for (int row = 0; row < bumpy::LevelBoard::rows; ++row) {
            REQUIRE(board.cell(col, row).size() == bumpy::LevelBoard::cell_bytes);
            any_object = any_object || board.object_index(col, row) != 0;
        }
    }
    REQUIRE(any_object);
}

TEST_CASE("level 1 board 0 carries its own blue gameplay palette in the DEC header") {
    const auto level = bumpy::LevelResources::load(root, 1);
    const auto dac = level.board(0).palette();  // 16 RGB triplets of 6-bit DAC values

    REQUIRE(dac.size() == bumpy::LevelBoard::palette_colors * 3);

    // Decoded from the board-0 header words (see analysis/specs/level-formats.md):
    // index 0 is black (the background base, word 0x0000); index 6 is the dark red of
    // the platforms (word 0x0400 -> R6=32); index 11 is the bright balloon blue
    // (word 0x0027 -> G6=16, B6=56). These are blue/colourful, not the brown of the
    // MONDE map palette -- proving the level does not inherit the map's colours.
    REQUIRE(dac[0] == 0);
    REQUIRE(dac[1] == 0);
    REQUIRE(dac[2] == 0);

    REQUIRE(dac[6 * 3 + 0] == 32);  // R
    REQUIRE(dac[6 * 3 + 1] == 0);   // G
    REQUIRE(dac[6 * 3 + 2] == 0);   // B

    REQUIRE(dac[11 * 3 + 0] == 0);   // R
    REQUIRE(dac[11 * 3 + 1] == 16);  // G
    REQUIRE(dac[11 * 3 + 2] == 56);  // B
}

TEST_CASE("level 4 is the 12-board (small) variant with a matching 12-board BUM") {
    const auto level = bumpy::LevelResources::load(root, 4);
    REQUIRE(level.board_count() == 12);
    REQUIRE_FALSE(level.bum_was_raw());
    REQUIRE(level.has_object_sheet());
}

TEST_CASE("level 7 has 12 DEC tile-boards but 15 BUM entity-boards") {
    // The two counts are not required to match; D7 is the confirmed counter-example.
    const auto level = bumpy::LevelResources::load(root, 7);
    REQUIRE(level.board_count() == 12);
    REQUIRE_FALSE(level.bum_was_raw());
    // 15 BUM boards: indices 0..14 are addressable.
    REQUIRE_NOTHROW(level.bum_board(14));
}

TEST_CASE("levels 6 and 9 ship a raw (pre-decoded) BUM detected by VEC-decode failure") {
    const auto level6 = bumpy::LevelResources::load(root, 6);
    REQUIRE(level6.board_count() == 15);
    REQUIRE(level6.bum_was_raw());
    REQUIRE_NOTHROW(level6.bum_board(14));

    const auto level9 = bumpy::LevelResources::load(root, 9);
    REQUIRE(level9.board_count() == 12);
    REQUIRE(level9.bum_was_raw());
}

TEST_CASE("level 3 ships no object sheet (0-byte PAV) but still has a DEC grid") {
    const auto level = bumpy::LevelResources::load(root, 3);
    REQUIRE(level.board_count() == 15);
    REQUIRE_FALSE(level.has_object_sheet());
    REQUIRE_THROWS(level.object_sheet());
}

TEST_CASE("level 1 board 0 decodes the three BUM entity layers and board params") {
    const auto level = bumpy::LevelResources::load(root, 1);
    const auto bum = level.bum_entities(0);

    // Layer A is the peg/bumper grid: a fixed 0/1 column pattern, all-1 bottom
    // row, and an always-empty spare column 7 (the values dumped from D1.BUM).
    REQUIRE(bum.layer_a(0, 0) == 0);
    REQUIRE(bum.layer_a(1, 0) == 1);
    REQUIRE(bum.layer_a(4, 0) == 1);
    REQUIRE(bum.layer_a(0, 5) == 1);  // bottom row is solid bumpers...
    REQUIRE(bum.layer_a(6, 5) == 1);
    for (int row = 0; row < bumpy::BumEntities::rows; ++row) {
        REQUIRE(bum.layer_a(7, row) == 0);  // ...except the spare column 7
    }

    // Layer B is empty on the first board.
    for (int col = 0; col < bumpy::BumEntities::columns; ++col) {
        for (int row = 0; row < bumpy::BumEntities::rows; ++row) {
            REQUIRE(bum.layer_b(col, row) == 0);
        }
    }

    // Layer C carries the sparse collectibles with their type codes at the exact
    // cells dumped from D1.BUM.
    REQUIRE(bum.layer_c(3, 0) == 0x1b);
    REQUIRE(bum.layer_c(4, 1) == 0x03);
    REQUIRE(bum.layer_c(1, 2) == 0x17);
    REQUIRE(bum.layer_c(6, 2) == 0x29);
    REQUIRE(bum.layer_c(4, 3) == 0x0f);
    REQUIRE(bum.layer_c(3, 4) == 0x0e);
    REQUIRE(bum.layer_c(0, 0) == 0);

    // Board params: 0/1 are 1-based cell indices (41, 44 -> bottom-row cells),
    // 2 and 4 are small counts.
    REQUIRE(bum.param(0) == 41);
    REQUIRE(bum.param(1) == 44);
    REQUIRE(bum.param(2) == 6);
    REQUIRE(bum.param(3) == 0);
    REQUIRE(bum.param(4) == 9);
    REQUIRE(bum.param(5) == 0);
}

TEST_CASE("BUM cell positions match the recovered DS:0x274 coordinate table") {
    REQUIRE(bumpy::bum_cell_position(0, 0).x == 8);
    REQUIRE(bumpy::bum_cell_position(0, 0).y == 8);
    REQUIRE(bumpy::bum_cell_position(6, 0).x == 248);
    REQUIRE(bumpy::bum_cell_position(3, 2).x == 128);
    REQUIRE(bumpy::bum_cell_position(3, 2).y == 72);
    REQUIRE(bumpy::bum_cell_position(0, 5).y == 168);
    REQUIRE(bumpy::bum_cell_position(7, 0).x == 288);  // col 7 is the rightmost column: 8 + 7*40
}

TEST_CASE("BUM entities decode from raw (pre-decoded) D6/D9 too") {
    const auto level6 = bumpy::LevelResources::load(root, 6);
    REQUIRE(level6.bum_was_raw());
    REQUIRE_NOTHROW(level6.bum_entities(0));
    REQUIRE_NOTHROW(level6.bum_entities(level6.bum_board_count() - 1));
}

TEST_CASE("BUM entity access is bounds-checked") {
    const auto level = bumpy::LevelResources::load(root, 1);
    const auto bum = level.bum_entities(0);
    REQUIRE_THROWS(bum.layer_a(-1, 0));
    REQUIRE_THROWS(bum.layer_a(bumpy::BumEntities::columns, 0));
    REQUIRE_THROWS(bum.layer_c(0, bumpy::BumEntities::rows));
    REQUIRE_THROWS(bum.param(bumpy::BumEntities::param_count));
    REQUIRE_THROWS(bumpy::bum_cell_position(8, 0));
}

TEST_CASE("board cell access is bounds-checked to the 20x13 grid") {
    const auto level = bumpy::LevelResources::load(root, 1);
    const auto& board = level.board(0);
    REQUIRE_THROWS(board.cell(-1, 0));
    REQUIRE_THROWS(board.cell(bumpy::LevelBoard::columns, 0));
    REQUIRE_THROWS(board.cell(0, bumpy::LevelBoard::rows));
    REQUIRE_THROWS(level.board(level.board_count()));
}

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

TEST_CASE("board cell access is bounds-checked to the 20x13 grid") {
    const auto level = bumpy::LevelResources::load(root, 1);
    const auto& board = level.board(0);
    REQUIRE_THROWS(board.cell(-1, 0));
    REQUIRE_THROWS(board.cell(bumpy::LevelBoard::columns, 0));
    REQUIRE_THROWS(board.cell(0, bumpy::LevelBoard::rows));
    REQUIRE_THROWS(level.board(level.board_count()));
}

#include "game/level_game.h"

#include "resources/level_resources.h"

#include <catch2/catch_test_macros.hpp>

using namespace bumpy;

namespace {

// A synthetic board: every plane-A cell is a basic lane (0x01), plane B/C empty,
// with a chosen start cell. Tests then drop a collectible where needed.
BumEntities lane_board(std::uint8_t start_cell, std::uint8_t required = 0) {
    BumEntities b{};
    for (int c = 0; c < 0x30; ++c) {
        b.bytes[c] = 0x01;  // plane A: basic lane everywhere
    }
    b.bytes[0x90] = static_cast<std::uint8_t>(start_cell + 1);  // header start = cell+1
    b.bytes[0x92] = required;                                   // required collectibles
    return b;
}

constexpr LevelInput none{};
constexpr LevelInput up{false, false, true, false, false};
constexpr LevelInput left{true, false, false, false, false};

}  // namespace

TEST_CASE("a new board positions the ball on its start cell") {
    LevelGame game(lane_board(0x14));  // row 2, col 4
    CHECK(game.ball_cell() == 0x14);
    // DS:0x274 slot for (col 4, row 2) + the (+7,+15) ball offset.
    CHECK(game.ball_x() == 8 + 4 * 40 + 7);
    CHECK(game.ball_y() == 8 + 2 * 32 + 15);
    CHECK(game.lives() == 5);
    CHECK(game.status() == LevelStatus::playing);
}

TEST_CASE("an idle ball on a lane stays put and never crashes") {
    LevelGame game(lane_board(0x14));
    const std::uint8_t start = game.ball_cell();
    for (int i = 0; i < 300; ++i) {
        game.tick(none);
    }
    CHECK(game.ball_cell() == start);            // idle-blinks in place (net-zero scripts)
    CHECK(game.status() == LevelStatus::playing);
}

TEST_CASE("holding LEFT rolls the ball one cell to the left") {
    // Regression for the transposed control scheme: the LEFT arrow (action bit 0x04)
    // must roll the ball left. It used to be the UP arrow that did this, because the
    // action-bit names were swapped (up<->left, down<->right) -- see build_input_bits.
    LevelGame game(lane_board(0x14));
    // Hold LEFT: idle -> hop up-left -> auto-roll-left (state 0x01), advancing one
    // cell. Run long enough for the 13-step roll script to finish.
    std::uint8_t reached = game.ball_cell();
    for (int i = 0; i < 16; ++i) {
        game.tick(left);
        reached = game.ball_cell();
    }
    CHECK(reached == 0x13);  // one column left of 0x14
}

TEST_CASE("rolling over a collectible scores it and clears the cell") {
    LevelGame game(lane_board(0x14, /*required=*/1));
    // Put a gem one cell to the left (0x13), where the LEFT roll passes.
    // (We can't mutate the board post-construction, so build it in.)
    BumEntities board = lane_board(0x14, 1);
    board.bytes[0x60 + 0x13] = 0x1a;  // plane C gem at cell 0x13
    LevelGame g(board);

    REQUIRE(g.collectibles_left() == 1);
    REQUIRE(g.collectible(/*col=*/3, /*row=*/2) == 0x1a);

    bool won = false;
    for (int i = 0; i < 30; ++i) {
        g.tick(left);
        if (g.status() == LevelStatus::won) {
            won = true;
        }
    }
    CHECK(g.collectibles_left() == 0);          // the last required gem was taken
    CHECK(g.collectible(3, 2) == 0);            // cell cleared
    CHECK(g.score() >= 250);                    // base pickup scored
    CHECK(won);                                 // all-collected -> win
}

TEST_CASE("the '#' tile grants a life and does not count toward the exit") {
    BumEntities board = lane_board(0x14, /*required=*/1);
    board.bytes[0x60 + 0x13] = 0x23;            // '#': extra life, free (not required)
    board.bytes[0x60 + 0x12] = 0x1a;            // the actual required gem, two cells left
    LevelGame g(board);

    const std::uint8_t lives0 = g.lives();
    for (int i = 0; i < 40; ++i) {
        g.tick(left);
    }
    CHECK(g.lives() == lives0 + 1);             // '#' added a life
}

TEST_CASE("score uses the documented per-tile values") {
    BumEntities board = lane_board(0x14, /*required=*/1);
    board.bytes[0x60 + 0x13] = 0x2f;            // '/': +10000
    LevelGame g(board);
    for (int i = 0; i < 30; ++i) {
        g.tick(left);
    }
    CHECK(g.score() == 10000);
}

TEST_CASE("real D1 board 0 runs for many frames without escaping the grid") {
    // Loads the user-supplied originals from the project root (as the other asset
    // tests do). Drives the ball through every input direction and checks it never
    // leaves the 6x8 cell grid and the game stays self-consistent.
    const auto level = bumpy::LevelResources::load(".", 1);
    LevelGame g(level.bum_entities(0));
    INFO("D1 board 0 start cell 0x" << std::hex << static_cast<int>(g.ball_cell()));
    REQUIRE(g.ball_cell() < 0x30);

    const LevelInput seq[] = {
        none, up, {true, false, false, false, false}, {false, true, false, false, false},
        {false, false, false, true, false}, {false, false, false, false, true}};
    for (int i = 0; i < 1000; ++i) {
        g.tick(seq[i % 6]);
        REQUIRE(g.ball_cell() < 0x30);  // never rolls off the board
        REQUIRE(g.collectibles_left() <= g.grid()[0x92]);
    }
}

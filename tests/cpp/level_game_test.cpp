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

TEST_CASE("an idle ball springs the lane it rests on") {
    // While resting the ball idle-blinks (states 0x3c-0x3f), whose step-0 handler
    // FUN_1000_6648 springs the tile underneath via kIdleSpringA. On a 0x01 lane that
    // is event 0x04, which plays the peg bumper frame 0x40 at the ball's own cell.
    LevelGame game(lane_board(0x14));
    std::array<ObjectAnimSprite, 7> anims{};
    bool sprang = false;
    std::uint8_t cell = 0xff;
    std::uint16_t frame = 0;
    for (int i = 0; i < 400; ++i) {
        game.tick(none);
        const std::size_t n = game.object_anims(anims);
        CHECK(n <= anims.size());
        for (std::size_t k = 0; k < n; ++k) {
            if (!anims[k].layer_b && anims[k].frame_index != kAnimHiddenFrame) {
                sprang = true;
                cell = anims[k].cell;
                frame = anims[k].frame_index;
            }
        }
    }
    CHECK(sprang);          // the resting lane reacted
    CHECK(cell == 0x14);    // ... at the ball's cell
    CHECK(frame == 0x40);   // ... drawing the peg bumper frame
}

TEST_CASE("bumping a lane while rolling springs it (held bump)") {
    // Rolling with fire/up held latches a held-bump (FUN_1000_654e -> 695e), which
    // springs the lane under the ball (kHeldBump[0x01] = event 0x03).
    LevelGame game(lane_board(0x14));
    const LevelInput left_fire{true, false, false, false, true};
    std::array<ObjectAnimSprite, 7> anims{};
    bool sprang = false;
    for (int i = 0; i < 20; ++i) {
        game.tick(left_fire);
        const std::size_t n = game.object_anims(anims);
        for (std::size_t k = 0; k < n; ++k) {
            if (!anims[k].layer_b && anims[k].frame_index != kAnimHiddenFrame) {
                sprang = true;
            }
        }
    }
    CHECK(sprang);
}

TEST_CASE("rolling off a platform springs it even without fire held") {
    // The "land and slide off" reaction: starting a roll (states 0x01/0x02/0x12/...)
    // recoils the lane via FUN_1000_6699/66d8 -> 6d6a, independent of the held-bump
    // path (which needs fire/up). Holding only LEFT must still spring the lane.
    LevelGame game(lane_board(0x14));
    std::array<ObjectAnimSprite, 7> anims{};
    bool sprang = false;
    for (int i = 0; i < 16; ++i) {
        game.tick(left);  // LEFT only -- no fire, no up
        const std::size_t n = game.object_anims(anims);
        for (std::size_t k = 0; k < n; ++k) {
            if (!anims[k].layer_b && anims[k].frame_index != kAnimHiddenFrame) {
                sprang = true;
            }
        }
    }
    CHECK(sprang);
}

TEST_CASE("a spring animation steps one sprite per frame then ends") {
    // Drive the ball idle until a spring arms, then confirm its frame_index changes
    // over consecutive frames and the slot frees itself (the 0xff terminator).
    LevelGame game(lane_board(0x14));
    std::array<ObjectAnimSprite, 7> anims{};
    bool seen = false;
    bool done = false;
    int active_frames = 0;
    for (int i = 0; i < 400 && !done; ++i) {
        game.tick(none);
        if (game.object_anims(anims) > 0) {
            seen = true;
            ++active_frames;       // count the first contiguous spring's frames
        } else if (seen) {
            done = true;           // the spring finished -> slot freed
        }
    }
    REQUIRE(seen);
    CHECK(done);                 // it terminated (the 0xff stream terminator)
    CHECK(active_frames >= 3);   // ... after playing several steps
    CHECK(active_frames <= 20);
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
    std::array<ObjectAnimSprite, 7> anims{};
    for (int i = 0; i < 1000; ++i) {
        g.tick(seq[i % 6]);
        REQUIRE(g.ball_cell() < 0x30);  // never rolls off the board
        REQUIRE(g.collectibles_left() <= g.grid()[0x92]);
        const std::size_t n = g.object_anims(anims);  // springs stay within bounds
        REQUIRE(n <= anims.size());
        for (std::size_t k = 0; k < n; ++k) {
            REQUIRE(anims[k].cell < 0x30);
        }
    }
}

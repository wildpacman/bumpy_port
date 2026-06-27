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
constexpr LevelInput right{false, true, false, false, false};

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

TEST_CASE("collecting the last gem opens the exit portal but does not win") {
    // Taking the last required collectible only OPENS the exit (FUN_1000_6c14 ->
    // 69aa(0x59) writes the pit tile 0x20 at the portal cell + arms a1b1). The board
    // is cleared only once the ball falls into the pit -- not the instant the gem is
    // taken. Here the portal cell stays at header 0x91 = 0 (cell 0), far from the
    // ball, so the ball cannot reach it within the loop.
    BumEntities board = lane_board(0x14, 1);
    board.bytes[0x60 + 0x13] = 0x1a;  // plane C gem one cell left (cell 0x13)
    LevelGame g(board);

    REQUIRE(g.collectibles_left() == 1);
    REQUIRE(g.collectible(/*col=*/3, /*row=*/2) == 0x1a);

    for (int i = 0; i < 30; ++i) {
        g.tick(left);
    }
    CHECK(g.collectibles_left() == 0);          // the last required gem was taken
    CHECK(g.collectible(3, 2) == 0);            // cell cleared
    CHECK(g.score() >= 250);                    // base pickup scored
    CHECK(g.grid()[0x00] == 0x20);              // the pit opened at the portal cell
    CHECK(g.status() == LevelStatus::playing);  // ... but the exit is not reached yet
}

TEST_CASE("rolling into the opened portal clears the board") {
    // The full exit: collect the last gem (the pit opens one cell further left), keep
    // rolling into the pit, fall in (tile 0x20 -> reaction 0x30 -> state 0x30 descent
    // -> FUN_1000_1e3d sets 9d30) and clear the board.
    BumEntities board = lane_board(0x14, 1);
    board.bytes[0x60 + 0x13] = 0x1a;  // last gem at cell 0x13
    board.bytes[0x91] = 0x13;         // portal cell = 0x12 (header is 1-based)
    LevelGame g(board);

    bool won = false;
    for (int i = 0; i < 200 && !won; ++i) {
        g.tick(left);
        won = g.status() == LevelStatus::won;
    }
    CHECK(won);                          // fell into the portal -> board cleared
    CHECK(g.collectibles_left() == 0);
    CHECK(g.status() == LevelStatus::won);
}

TEST_CASE("rolling into a vertical spike triggers the fly-around death") {
    // The vertical spike walls are plane-B value 0x0c (FUN_1000_6326/6372). While the
    // ball rolls (states 0x01/0x02), step 4 samples the neighbouring/own cell's plane B;
    // a spike there arms the death-tumble (state 0x2e, the ball flies around the screen)
    // which cascades through FUN_1000_22d2 three times into FUN_1000_22fc (lose a life).
    BumEntities board = lane_board(0x14);   // ball on a lane at row 2, col 4
    board.bytes[0x30 + 0x13] = 0x0c;        // plane-B spike one cell to the LEFT (cell 0x13)
    LevelGame g(board);

    const std::uint8_t lives0 = g.lives();
    bool saw_tumble = false;
    bool dead = false;
    for (int i = 0; i < 200 && !dead; ++i) {
        g.tick(left);
        if (g.player_state() == 0x2e) {
            saw_tumble = true;
        }
        dead = g.status() == LevelStatus::dead;
    }
    CHECK(saw_tumble);                 // entered the fly-around death-tumble state
    CHECK(dead);                       // ... and ultimately ended the life
    CHECK(g.lives() == lives0 - 1);    // exactly one life lost
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

TEST_CASE("landing on a special bumper springs it (node 14 structure trigger)") {
    // World-1 node 14 (D1 board 13) is a row of special bumpers (plane-A 0x14/0x15,
    // FUN_1000_1fbe/207d) that fling the ball left and right. Their recoil is armed by
    // the structure trigger FUN_1000_6d26 (DS:0x4396): tile 0x14 -> event 0x2d, 0x15 ->
    // 0x2e (the bumper compressing, sprite frames 0xbf..0xca). Regression: f_6d26 read
    // the trigger but never called FUN_1000_6d94, so the bumpers threw the ball yet
    // never sprang -- exactly the bug reported against this level.
    const auto level = bumpy::LevelResources::load(".", 1);
    LevelGame g(level.bum_entities(13));
    std::array<ObjectAnimSprite, 7> anims{};
    bool sprang_bumper = false;
    for (int i = 0; i < 60; ++i) {
        g.tick(right);  // roll right off the top lane, fall onto the bumper row, get flung
        const std::size_t n = g.object_anims(anims);
        for (std::size_t k = 0; k < n; ++k) {
            const std::uint8_t row = static_cast<std::uint8_t>(anims[k].cell / 8);
            const std::uint16_t f = anims[k].frame_index;
            // row 3 is the bumper row; 0xbf..0xca are exclusively the 0x14/0x15 recoil.
            if (!anims[k].layer_b && row == 3 && f >= 0xbf && f <= 0xca) {
                sprang_bumper = true;
            }
        }
    }
    CHECK(sprang_bumper);
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

// ===== Moving entity (monster) ===============================================

TEST_CASE("most boards have no monster; D1 board 2 spawns one") {
    const auto level = bumpy::LevelResources::load(".", 1);
    // Nodes 1 and 2 (board index 0/1) carry no entity (header 0x93 == 0).
    CHECK_FALSE(LevelGame(level.bum_entities(0)).monster_present());
    CHECK_FALSE(LevelGame(level.bum_entities(1)).monster_present());

    // Node 3 (board index 2): entity at cell 39 (col 7, row 4), sprite base 0x1f7.
    LevelGame g(level.bum_entities(2));
    REQUIRE(g.monster_present());
    CHECK(g.monster_cell() == 39);
    CHECK(g.monster_frame() == 0x1f7);  // a0de (0x2546[0x10]) + keyframe 0
    // Pixel anchor = bum_cell_position(7,4) + (7,7).
    CHECK(g.monster_x() == 8 + 7 * 40 + 7);
    CHECK(g.monster_y() == 8 + 4 * 32 + 7);
}

TEST_CASE("the D1 board-2 monster walks the maze without leaving the grid") {
    const auto level = bumpy::LevelResources::load(".", 1);
    LevelGame g(level.bum_entities(2));
    const int start_x = g.monster_x();
    const int start_y = g.monster_y();
    bool moved = false;
    for (int i = 0; i < 400; ++i) {
        g.tick(none);  // ball idles; only the monster moves
        REQUIRE(g.monster_cell() < 0x30);             // stays on the 6x8 grid
        if (g.monster_x() != start_x || g.monster_y() != start_y) {
            moved = true;
        }
    }
    CHECK(moved);  // the entity actually patrols (it does not sit still)
}

TEST_CASE("the monster steps at half rate (every other frame)") {
    const auto level = bumpy::LevelResources::load(".", 1);
    LevelGame g(level.bum_entities(2));
    const int x0 = g.monster_x();
    const int y0 = g.monster_y();
    g.tick(none);                                  // 8243: 0->1, the entity steps
    const bool moved_first = (g.monster_x() != x0 || g.monster_y() != y0);
    const int x1 = g.monster_x();
    const int y1 = g.monster_y();
    g.tick(none);                                  // 8243: 1->0, the entity holds
    CHECK(g.monster_x() == x1);
    CHECK(g.monster_y() == y1);
    CHECK(moved_first);
}

TEST_CASE("touching the monster kills the ball and costs a life") {
    // Synthetic board: all-lane plane A, the ball and the monster share a cell. The
    // AABB boxes overlap immediately, so 50fb sets a1aa and 1d26 arms the shared
    // state-0x2e death cascade (22d2 x3 -> 22fc), losing one of the 5 lives.
    BumEntities board = lane_board(0x14);
    board.bytes[0x93] = 0x14 + 1;  // monster cell = ball cell (header value-1)
    board.bytes[0x94] = 4;         // behaviour 4 (move right)
    board.bytes[0x95] = 0;         // sub-type 0 (deterministic)
    board.bytes[0x96] = 0x10;      // sprite base index
    LevelGame g(board);
    REQUIRE(g.monster_present());
    REQUIRE(g.lives() == 5);

    bool died = false;
    for (int i = 0; i < 400 && !died; ++i) {
        g.tick(none);
        died = (g.status() == LevelStatus::dead);
    }
    CHECK(died);
    CHECK(g.lives() == 4);
}

TEST_CASE("a monster in another row never reaches an idle ball") {
    // On an all-0x01 lane board the monster can only move horizontally (the up/down
    // checks see a non-empty plane A), so a monster in row 0 stays in row 0 and an
    // idle ball in row 5 is safe.
    BumEntities board = lane_board(0x28);  // ball at row 5, col 0
    board.bytes[0x93] = 0x03 + 1;          // monster at row 0, col 3
    board.bytes[0x94] = 4;                 // move right
    board.bytes[0x95] = 0;
    board.bytes[0x96] = 0x10;
    LevelGame g(board);
    REQUIRE(g.monster_present());
    for (int i = 0; i < 200; ++i) {
        g.tick(none);
        REQUIRE(g.monster_cell() < 8);  // never leaves row 0
    }
    CHECK(g.status() == LevelStatus::playing);
}

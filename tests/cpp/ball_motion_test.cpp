#include "game/ball_motion.h"

#include <catch2/catch_test_macros.hpp>

using namespace bumpy;

namespace {

// Arm a state and play its whole script, returning the net pixel displacement.
struct Played {
    int dx{};
    int dy{};
    int steps{};
    std::int16_t first_frame{};
    std::int16_t last_frame{};
};

Played play(std::uint8_t state) {
    BallMotion ball;
    ball.arm(state);
    Played p;
    p.first_frame = ball.current_step() ? ball.current_step()->frame : -1;
    const int x0 = ball.x;
    const int y0 = ball.y;
    while (ball.moving()) {
        ball.step();
        ++p.steps;
    }
    p.dx = ball.x - x0;
    p.dy = ball.y - y0;
    p.last_frame = ball.frame;
    return p;
}

}  // namespace

TEST_CASE("arm loads a state's move script") {
    BallMotion ball;
    ball.arm(0x02);
    CHECK(ball.state == 0x02);
    CHECK(ball.steps_left == 13);
    CHECK(ball.facing == 0);
    REQUIRE(ball.current_step() != nullptr);
    CHECK(ball.current_step()->frame == 1);  // idx 0x02 starts on frame 1
}

TEST_CASE("playing a roll script advances exactly one cell") {
    // idx 0x02 rolls one cell right (+40,0); idx 0x01 is its mirror -> one cell left.
    const auto right = play(0x02);
    CHECK(right.steps == 13);
    CHECK(right.dx == 40);
    CHECK(right.dy == 0);

    const auto left = play(0x01);
    CHECK(left.steps == 13);
    CHECK(left.dx == -40);  // facing negates dx
    CHECK(left.dy == 0);
}

TEST_CASE("hop scripts move one cell vertically") {
    CHECK(play(0x03).dy == -32);  // hop up one cell
    CHECK(play(0x03).dx == 0);
    CHECK(play(0x04).dy == 32);   // fall/hop down one cell
    CHECK(play(0x1b).dx == 80);   // long bounce: two cells right
    CHECK(play(0x1a).dx == -80);  // its mirror partner: two cells left
}

TEST_CASE("step sets the sprite frame and resets the counter at the end") {
    BallMotion ball;
    ball.arm(0x02);
    ball.step();
    CHECK(ball.frame == 1);       // first keyframe frame
    CHECK(ball.step_index == 1);  // 792a increments mid-script
    while (ball.moving()) {
        ball.step();
    }
    CHECK(ball.steps_left == 0);
    CHECK(ball.step_index == 0);  // reset to 0 when the script finishes
    CHECK(ball.frame == 0);       // idx 0x02's last keyframe
}

TEST_CASE("scriptless states keep the running script and do not move") {
    BallMotion ball;
    ball.arm(0x02);  // arm a real roll
    const int x_before = ball.x;
    ball.arm(0x05);  // 0x05 is scriptless: sets the state, keeps the script
    CHECK(ball.state == 0x05);
    CHECK(ball.steps_left == 13);  // unchanged
    CHECK_FALSE(ball.moving());
    ball.step();
    CHECK(ball.x == x_before);  // step is a no-op in a scriptless state
    CHECK(ball.steps_left == 13);
}

TEST_CASE("a sub-step lock inhibits arm and step") {
    BallMotion ball;
    ball.substep_lock = 1;
    ball.arm(0x02);
    CHECK(ball.state == 0);        // arm did not change state
    CHECK(ball.steps_left == 0);   // no script armed
    ball.substep_lock = 0;
    ball.arm(0x02);
    ball.substep_lock = 1;
    const int x_before = ball.x;
    ball.step();
    CHECK(ball.x == x_before);     // step did nothing under the lock
    CHECK(ball.steps_left == 13);
}

TEST_CASE("set_cell maps a board cell to its screen slot and ball offset") {
    BallMotion ball;
    ball.set_cell(0);  // col 0, row 0
    CHECK(ball.cell_col == 0);
    CHECK(ball.cell_row == 0);
    CHECK(ball.x == 8 + 7);
    CHECK(ball.y == 8 + 15);

    ball.set_cell(9);  // col 1, row 1
    CHECK(ball.cell_col == 1);
    CHECK(ball.cell_row == 1);
    CHECK(ball.x == 8 + 40 + 7);
    CHECK(ball.y == 8 + 32 + 15);

    ball.set_cell(15);  // col 7, row 1 -> the spare column slot at x = 32
    CHECK(ball.cell_col == 7);
    CHECK(ball.x == 32 + 7);
}

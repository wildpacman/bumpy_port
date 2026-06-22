#include "game/move_scripts.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>

using namespace bumpy;

namespace {

// Net pixel displacement of a script, with dx taken at the script's baked facing
// (mirror non-zero negates dx, exactly as FUN_1000_13df applies it).
std::pair<int, int> net(const MoveScript& s) {
    int x = 0;
    int y = 0;
    for (int i = 0; i < s.count; ++i) {
        x += s.mirror ? -s.steps[i].dx : s.steps[i].dx;
        y += s.steps[i].dy;
    }
    return {x, y};
}

// Net as authored (dx un-mirrored), matching how move-scripts.md prints `net=`.
std::pair<int, int> raw_net(const MoveScript& s) {
    int x = 0;
    int y = 0;
    for (int i = 0; i < s.count; ++i) {
        x += s.steps[i].dx;
        y += s.steps[i].dy;
    }
    return {x, y};
}

}  // namespace

TEST_CASE("move-script table is baked with the documented counts and facings") {
    // Spot-check entries against analysis/specs/move-scripts.md.
    CHECK(move_script(0x00).count == 14);
    CHECK(move_script(0x00).mirror == 0);
    CHECK(move_script(0x01).count == 13);
    CHECK(move_script(0x01).mirror == 1);
    CHECK(move_script(0x02).count == 13);
    CHECK(move_script(0x02).mirror == 0);
    CHECK(move_script(0x1a).count == 13);
    CHECK(move_script(0x1a).mirror == 1);
    CHECK(move_script(0x2e).count == 26);  // the death tumble
}

TEST_CASE("table gaps are the scriptless / animation-only states") {
    for (std::uint8_t idx : {0x05, 0x0b, 0x1c, 0x23, 0x24}) {
        INFO("state 0x" << std::hex << static_cast<int>(idx));
        CHECK_FALSE(move_script(idx).present());
    }
}

TEST_CASE("raw net displacement matches the move-scripts.md annotations") {
    // The `net=(x,y)` column printed by dump_move_scripts.py (un-mirrored dx).
    CHECK(raw_net(move_script(0x00)) == std::pair{0, 0});
    CHECK(raw_net(move_script(0x01)) == std::pair{40, 0});   // cell(+1,0)
    CHECK(raw_net(move_script(0x02)) == std::pair{40, 0});   // cell(+1,0)
    CHECK(raw_net(move_script(0x03)) == std::pair{0, -32});  // cell(0,-1) hop up
    CHECK(raw_net(move_script(0x04)) == std::pair{0, 32});   // cell(0,+1) hop down
    CHECK(raw_net(move_script(0x1a)) == std::pair{80, 0});   // cell(+2,0)
    CHECK(raw_net(move_script(0x2e)) == std::pair{66, 0});   // death tumble
}

TEST_CASE("a mirrored lane roll moves the ball one cell against its authored dx") {
    // idx 0x01 is the mirror=1 partner of idx 0x02; both author +40 (one cell to
    // the right), but the mirror flips 0x01 to roll one cell left (-40).
    CHECK(net(move_script(0x01)) == std::pair{-40, 0});
    CHECK(net(move_script(0x02)) == std::pair{40, 0});
}

TEST_CASE("the hidden sentinel frame 100 appears where the spec documents it") {
    // Move-scripts that end by vanishing the ball use frame 0x64 (=100), which the
    // blitter skips (FUN_1000_1cb2). idx 0x10 and 0x2c both terminate on it.
    const auto& s10 = move_script(0x10);
    REQUIRE(s10.present());
    CHECK(s10.steps[s10.count - 1].frame == 100);
}

TEST_CASE("out-of-range indices return a null script") {
    CHECK_FALSE(move_script(0x41).present());
    CHECK_FALSE(move_script(0xff).present());
}

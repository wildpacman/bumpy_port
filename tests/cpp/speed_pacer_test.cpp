#include <catch2/catch_test_macros.hpp>

#include "game/speed_pacer.h"

// The LEVEL menu item selects an 8-bit speed pattern (DS:0x11b2) that FUN_1000_1349
// rotates once per in-level frame, waiting (low bit ? 2 : 1) vertical retraces.
// See analysis/specs/menu-behavior.md ("Difficulty selection").

TEST_CASE("difficulty patterns match the DS:0x11b2 table") {
    REQUIRE(bumpy::level_speed_pattern(0) == 0xff);  // EASY
    REQUIRE(bumpy::level_speed_pattern(1) == 0xaa);  // MEDIUM
    REQUIRE(bumpy::level_speed_pattern(2) == 0x00);  // HARD
    REQUIRE(bumpy::level_speed_pattern(9) == 0xff);  // out of range -> EASY
}

TEST_CASE("EASY paces every in-level frame at two retraces (35 Hz)") {
    bumpy::SpeedPacer pacer;
    pacer.reset(bumpy::level_speed_pattern(0));  // 0xff
    for (int i = 0; i < 16; ++i) {
        REQUIRE(pacer.step() == 2);
    }
    REQUIRE(pacer.pattern() == 0xff);  // rotation of an all-ones mask is a no-op
}

TEST_CASE("HARD paces every in-level frame at one retrace (70 Hz)") {
    bumpy::SpeedPacer pacer;
    pacer.reset(bumpy::level_speed_pattern(2));  // 0x00
    for (int i = 0; i < 16; ++i) {
        REQUIRE(pacer.step() == 1);
    }
    REQUIRE(pacer.pattern() == 0x00);
}

TEST_CASE("MEDIUM alternates one and two retraces as 0xaa rotates") {
    bumpy::SpeedPacer pacer;
    pacer.reset(bumpy::level_speed_pattern(1));  // 0xaa = 1010_1010, low bit 0 first
    REQUIRE(pacer.step() == 1);   // bit 0 = 0 -> 1 wait; rotate -> 0x55
    REQUIRE(pacer.pattern() == 0x55);
    REQUIRE(pacer.step() == 2);   // bit 0 = 1 -> 2 waits; rotate -> 0xaa
    REQUIRE(pacer.pattern() == 0xaa);
    REQUIRE(pacer.step() == 1);
    REQUIRE(pacer.step() == 2);
    // Over 8 frames a 4-bit mask spends exactly half its frames double-waiting.
    int total = 0;
    for (int i = 0; i < 8; ++i) {
        total += pacer.step();
    }
    REQUIRE(total == 12);  // 4 frames x1 + 4 frames x2
}

TEST_CASE("a fresh pacer defaults to EASY before any reset") {
    bumpy::SpeedPacer pacer;
    REQUIRE(pacer.pattern() == 0xff);
    REQUIRE(pacer.step() == 2);
}

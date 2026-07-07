#include <catch2/catch_test_macros.hpp>

#include "video/viewport.h"

using bumpy::compute_letterbox_viewport;

TEST_CASE("exact fit fills the window") {
    const auto vp = compute_letterbox_viewport(640, 400, 320, 200);
    REQUIRE(vp.x == 0);
    REQUIRE(vp.y == 0);
    REQUIRE(vp.w == 640);
    REQUIRE(vp.h == 400);
}

TEST_CASE("wider window letterboxes left and right") {
    // 16:9 1920x1080 window, 16:10 logical: height-limited, 1728x1080 centred.
    const auto vp = compute_letterbox_viewport(1920, 1080, 320, 200);
    REQUIRE(vp.h == 1080);
    REQUIRE(vp.w == 1728);
    REQUIRE(vp.x == 96);
    REQUIRE(vp.y == 0);
}

TEST_CASE("taller window letterboxes top and bottom") {
    const auto vp = compute_letterbox_viewport(640, 600, 320, 200);
    REQUIRE(vp.w == 640);
    REQUIRE(vp.h == 400);
    REQUIRE(vp.x == 0);
    REQUIRE(vp.y == 100);
}

TEST_CASE("4:3 logical (Alt+A CRT aspect) letterboxes a 16:10 window") {
    const auto vp = compute_letterbox_viewport(960, 600, 320, 240);
    REQUIRE(vp.h == 600);
    REQUIRE(vp.w == 800);
    REQUIRE(vp.x == 80);
}

TEST_CASE("degenerate sizes yield an empty viewport") {
    const auto vp = compute_letterbox_viewport(0, 600, 320, 200);
    REQUIRE(vp.w == 0);
    REQUIRE(vp.h == 0);
}

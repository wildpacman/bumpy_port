#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "resources/font.h"
#include "resources/menu_resources.h"  // decode_sprite_archive
#include "video/hud.h"

namespace {

// Count non-zero pixels in the top HUD strip (the lives row sits at y = 0).
int top_strip_pixels(bumpy::IndexedFramebuffer& frame) {
    int set = 0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (frame.pixel(x, y) != 0) {
                ++set;
            }
        }
    }
    return set;
}

}  // namespace

TEST_CASE("draw_lives blits one life icon per remaining life along the top") {
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");

    bumpy::IndexedFramebuffer none(320, 200);
    bumpy::draw_lives(bank.bytes(), 0, none);
    REQUIRE(top_strip_pixels(none) == 0);  // no lives -> nothing drawn

    bumpy::IndexedFramebuffer two(320, 200);
    bumpy::draw_lives(bank.bytes(), 2, two);
    const int two_px = top_strip_pixels(two);
    REQUIRE(two_px > 0);

    bumpy::IndexedFramebuffer five(320, 200);
    bumpy::draw_lives(bank.bytes(), 5, five);
    REQUIRE(top_strip_pixels(five) > two_px);  // more lives -> more icons drawn
}

TEST_CASE("draw_score renders 7 zero-padded digits at the cursor in the score colour") {
    const auto font = bumpy::Font::load("DDFNT2.CAR");
    bumpy::IndexedFramebuffer frame(320, 200);  // blank (index 0)

    bumpy::draw_score(font, 0, bumpy::kMapScoreX, bumpy::kMapScoreBaselineY, 14, frame);

    // The first '0' (7x7, top row ".#####.") draws at cursor x=1, top = baseline - ascent
    // = 8 - 7 = 1. Row 0 sets cols 1..5 -> tx 2..6 at ty 1 in colour 14; col 0 (tx 1) stays
    // background.
    REQUIRE(frame.pixel(1, 1) == 0);
    REQUIRE(frame.pixel(2, 1) == 14);
    REQUIRE(frame.pixel(6, 1) == 14);

    // Seven zeros, each advancing 8px, span ~x=1..56; each '0' has ~30 set pixels.
    int colored = 0;
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (frame.pixel(x, y) == 14) {
                ++colored;
            }
        }
    }
    REQUIRE(colored > 100);
}

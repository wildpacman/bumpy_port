#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "video/screen_transition.h"

using bumpy::IndexedFramebuffer;
using bumpy::Rgba;
using bumpy::ScreenTransition;

namespace {

// A 320x200 frame filled with a non-zero index, so the black (index 0) border is
// distinguishable from the still-visible centre.
IndexedFramebuffer make_frame(std::uint8_t fill) {
    IndexedFramebuffer frame(320, 200);
    frame.clear(fill);
    return frame;
}

}  // namespace

TEST_CASE("a fresh ScreenTransition is inactive") {
    ScreenTransition t;
    REQUIRE_FALSE(t.active());
}

TEST_CASE("begin arms the wipe at the outermost ring") {
    ScreenTransition t;
    t.begin(make_frame(7));
    REQUIRE(t.active());
    REQUIRE(t.step() == 1);
}

TEST_CASE("the border grows one cell per step from every edge") {
    ScreenTransition t;
    t.begin(make_frame(7));

    IndexedFramebuffer out = make_frame(0xFF);
    t.render(out);  // step 1: border is one 16x8 cell thick on each side

    // Outer pixels are blacked (index 0); just inside the border still shows the snapshot.
    REQUIRE(out.pixel(0, 0) == 0);
    REQUIRE(out.pixel(15, 7) == 0);          // last px of the 16x8 corner cell
    REQUIRE(out.pixel(16, 8) == 7);          // first still-visible interior px
    REQUIRE(out.pixel(319, 199) == 0);       // opposite corner
    REQUIRE(out.pixel(303, 191) == 7);       // just inside the far corner (320-16, 200-8)
    REQUIRE(out.pixel(160, 100) == 7);       // centre still visible
}

TEST_CASE("each step adds exactly one more cell of border") {
    ScreenTransition t;
    t.begin(make_frame(7));
    t.advance();  // step 2
    REQUIRE(t.step() == 2);

    IndexedFramebuffer out = make_frame(0xFF);
    t.render(out);
    REQUIRE(out.pixel(31, 15) == 0);   // two cells in is still black
    REQUIRE(out.pixel(32, 16) == 7);   // the third cell shows the snapshot
}

TEST_CASE("the wipe ends fully black after the last step") {
    ScreenTransition t;
    t.begin(make_frame(7));

    int rendered_steps = 0;
    IndexedFramebuffer out = make_frame(0xFF);
    while (t.active()) {
        t.render(out);
        ++rendered_steps;
        t.advance();
    }
    REQUIRE(rendered_steps == ScreenTransition::kSteps);  // 10 visible steps
    REQUIRE_FALSE(t.active());

    // The final rendered frame is entirely black (index 0) -- centre included.
    REQUIRE(out.pixel(160, 100) == 0);
    REQUIRE(out.pixel(159, 96) == 0);
    REQUIRE(out.pixel(0, 0) == 0);
}

TEST_CASE("render restores the outgoing palette so index 0 is the outgoing black") {
    IndexedFramebuffer frame = make_frame(7);
    frame.set_palette(0, Rgba{1, 2, 3, 255});  // a distinctive "black" for the outgoing screen
    ScreenTransition t;
    t.begin(frame);

    IndexedFramebuffer out(320, 200);  // default (zeroed) palette
    t.render(out);
    const Rgba c = out.palette()[0];
    REQUIRE(c.r == 1);
    REQUIRE(c.g == 2);
    REQUIRE(c.b == 3);
}

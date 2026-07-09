#include <catch2/catch_test_macros.hpp>

#include "game/settings_overlay.h"

using bumpy::MenuInput;
using bumpy::SettingsEvent;
using bumpy::SettingsOverlay;
using bumpy::SettingsPage;

namespace {
// One debounced key press: the action frame, then a released frame to clear the latch.
SettingsEvent press(SettingsOverlay& o, const MenuInput& in, bool gl = true) {
    const SettingsEvent e = o.update(in, gl);
    o.update(MenuInput{}, gl);  // release
    return e;
}
}  // namespace

TEST_CASE("overlay starts on the root page at cursor 0") {
    SettingsOverlay o;
    REQUIRE(o.page() == SettingsPage::root);
    REQUIRE(o.cursor_row() == 0);
}

TEST_CASE("root cursor moves and wraps") {
    SettingsOverlay o;
    REQUIRE(press(o, MenuInput{.up = true}) == SettingsEvent::none);
    REQUIRE(o.cursor_row() == bumpy::kRootRowCount - 1);  // wrap up to QUIT
    REQUIRE(press(o, MenuInput{.down = true}) == SettingsEvent::none);
    REQUIRE(o.cursor_row() == 0);                          // wrap back to VIDEO
}

TEST_CASE("root enters each sub-page and backs out to root") {
    SettingsOverlay o;
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::none);  // VIDEO
    REQUIRE(o.page() == SettingsPage::video);
    REQUIRE(press(o, MenuInput{.cancel = true}) == SettingsEvent::none);   // back
    REQUIRE(o.page() == SettingsPage::root);

    REQUIRE(press(o, MenuInput{.down = true}) == SettingsEvent::none);     // -> AUDIO row
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::none);
    REQUIRE(o.page() == SettingsPage::audio);
    REQUIRE(press(o, MenuInput{.left = true}) == SettingsEvent::none);     // left == back
    REQUIRE(o.page() == SettingsPage::root);
}

TEST_CASE("video rows emit the right toggle events") {
    SettingsOverlay o;
    press(o, MenuInput{.confirm = true});                 // enter VIDEO
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_3d);      // row 0
    press(o, MenuInput{.down = true});
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_aspect);  // row 1
    press(o, MenuInput{.down = true});
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_fullscreen); // row 2
}

TEST_CASE("3D toggle is suppressed when GL is unavailable") {
    SettingsOverlay o;
    press(o, MenuInput{.confirm = true});                 // enter VIDEO, row 0 (3D)
    REQUIRE(press(o, MenuInput{.confirm = true}, /*gl=*/false) == SettingsEvent::none);
}

TEST_CASE("audio rows emit the right toggle events") {
    SettingsOverlay o;
    press(o, MenuInput{.down = true});                    // root -> AUDIO row
    press(o, MenuInput{.confirm = true});                 // enter AUDIO
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_music);   // row 0
    press(o, MenuInput{.down = true});
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_sfx);     // row 1
}

TEST_CASE("QUIT emits quit; back from root closes") {
    SettingsOverlay o;
    // Move to QUIT (row 3) and confirm.
    press(o, MenuInput{.up = true});                      // wrap up to QUIT
    REQUIRE(o.cursor_row() == bumpy::kRootRowCount - 1);
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::quit);

    SettingsOverlay o2;
    REQUIRE(press(o2, MenuInput{.cancel = true}) == SettingsEvent::close);
    REQUIRE(press(o2, MenuInput{.left = true}) == SettingsEvent::close);
}

TEST_CASE("a held key fires once until released") {
    SettingsOverlay o;
    REQUIRE(o.update(MenuInput{.down = true}, true) == SettingsEvent::none);
    REQUIRE(o.cursor_row() == 1);
    REQUIRE(o.update(MenuInput{.down = true}, true) == SettingsEvent::none);  // still held
    REQUIRE(o.cursor_row() == 1);
    REQUIRE(o.update(MenuInput{}, true) == SettingsEvent::none);              // release
    REQUIRE(o.update(MenuInput{.down = true}, true) == SettingsEvent::none);
    REQUIRE(o.cursor_row() == 2);
}

TEST_CASE("passwords page ignores navigation and confirm; backs out to root") {
    SettingsOverlay o;
    press(o, MenuInput{.down = true});                    // root -> AUDIO row
    press(o, MenuInput{.down = true});                    // -> PASSWORDS row (row 2)
    REQUIRE(o.cursor_row() == 2);
    press(o, MenuInput{.confirm = true});                 // enter PASSWORDS
    REQUIRE(o.page() == SettingsPage::passwords);
    REQUIRE(press(o, MenuInput{.down = true}) == SettingsEvent::none);   // no rows to move
    REQUIRE(o.cursor_row() == 0);
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::none);// read-only
    REQUIRE(press(o, MenuInput{.cancel = true}) == SettingsEvent::none); // back
    REQUIRE(o.page() == SettingsPage::root);
}

TEST_CASE("reset returns to root at cursor 0") {
    SettingsOverlay o;
    press(o, MenuInput{.down = true});
    press(o, MenuInput{.confirm = true});
    o.reset();
    REQUIRE(o.page() == SettingsPage::root);
    REQUIRE(o.cursor_row() == 0);
}

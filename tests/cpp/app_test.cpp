#include <catch2/catch_test_macros.hpp>

#include "game/app.h"

namespace {

// Drive the App to the level screen the way a player would: confirm the top menu
// item, then release the key (the menu debounces a held confirm).
void enter_level(bumpy::App& app) {
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::level);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
}

}  // namespace

TEST_CASE("app starts on the menu at board zero") {
    const bumpy::App app(15);

    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.board_index() == 0);
    REQUIRE(app.board_count() == 15);
}

TEST_CASE("confirming the top menu item enters the level on board zero") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::level);
    REQUIRE(app.board_index() == 0);
}

TEST_CASE("cancel on the level screen returns to the menu") {
    bumpy::App app(15);
    enter_level(app);

    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
}

TEST_CASE("cancel on the menu quits") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("quitting from the menu's exit row propagates") {
    bumpy::App app(15);

    for (int row = 0; row < 3; ++row) {
        REQUIRE(app.update(bumpy::MenuInput{.down = true}) == bumpy::AppOutcome::running);
        REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    }
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("a held cancel does not bounce level -> menu -> quit") {
    bumpy::App app(15);
    enter_level(app);

    // First cancel edge: level -> menu.
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);

    // Still holding cancel: must not quit until the key is released.
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);

    // Release, then a fresh cancel edge quits from the menu.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("left/right page the boards on the level screen with wraparound") {
    bumpy::App app(3);
    enter_level(app);

    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.board_index() == 1);

    // Held right is debounced -- no further advance until release.
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.board_index() == 1);

    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.board_index() == 2);

    // Wrap forward 2 -> 0.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.board_index() == 0);

    // Wrap backward 0 -> 2.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.left = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.board_index() == 2);
}

TEST_CASE("left/right do nothing on the menu screen") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.left = true, .right = true}) ==
            bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.board_index() == 0);
}

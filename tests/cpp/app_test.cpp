#include <catch2/catch_test_macros.hpp>

#include "game/app.h"

namespace {

// Drive the App from the menu to the level the way a player would: confirm "start"
// (menu -> world map), release, then fire on node 1 (map -> level board 0), release.
void enter_level(bumpy::App& app) {
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);  // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::level);
    REQUIRE(app.board_index() == 0);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);  // release
}

}  // namespace

TEST_CASE("app starts on the menu at board zero") {
    const bumpy::App app(15);

    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.board_index() == 0);
    REQUIRE(app.board_count() == 15);
}

TEST_CASE("confirming the top menu item enters the world map on node 1") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE(app.world_map().current_node() == 1);
}

TEST_CASE("firing on a map node enters that node's board") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);    // node 2
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // fire
    REQUIRE(app.screen() == bumpy::Screen::level);
    REQUIRE(app.board_index() == 1);  // node 2 -> board 1
}

TEST_CASE("cancel on the world map returns to the menu") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
}

TEST_CASE("re-entering the map resets the avatar to node 1") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);    // node 2
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);   // -> menu
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.world_map().current_node() == 1);
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

TEST_CASE("a held confirm does not bounce menu -> map -> level") {
    bumpy::App app(15);

    // First confirm edge: menu -> map.
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);

    // Still holding confirm: must not select a board until the key is released.
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);

    // Release, then a fresh confirm edge enters the level.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::level);
}

TEST_CASE("a held cancel does not bounce map -> menu -> quit") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release

    // First cancel edge: map -> menu.
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);

    // Still holding cancel: must not quit until released.
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);

    // Release, then a fresh cancel edge quits from the menu.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("left/right do nothing on the menu screen") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.left = true, .right = true}) ==
            bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.board_index() == 0);
}

#include <catch2/catch_test_macros.hpp>

#include "game/menu.h"

TEST_CASE("menu state exposes the confirmed initial game-menu view") {
    const bumpy::Menu menu;

    REQUIRE(menu.view().draw_title);
    REQUIRE(menu.view().draw_cursor_marker);
    REQUIRE(menu.view().cursor_row == 0);
    REQUIRE(menu.cycle_value() == 0);
}

TEST_CASE("menu state moves vertically without wrapping") {
    bumpy::Menu menu;

    REQUIRE(menu.update(bumpy::MenuInput{.up = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 0);

    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 1);

    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 3);
}

TEST_CASE("menu state consumes a held key until release") {
    bumpy::Menu menu;

    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 1);
    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 1);

    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 2);
}

TEST_CASE("menu state preserves original input priority") {
    bumpy::Menu menu;

    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 1);

    REQUIRE(menu.update(bumpy::MenuInput{.up = true, .down = true, .confirm = true}) ==
            bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 0);
}

TEST_CASE("menu state ignores inputs not present in the confirmed menu bit field") {
    bumpy::Menu menu;

    REQUIRE(menu.update(bumpy::MenuInput{.left = true, .right = true, .cancel = true}) ==
            bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 0);
    REQUIRE(menu.cycle_value() == 0);
    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 1);
}

TEST_CASE("menu state cycles the confirmed row-two value without exiting") {
    bumpy::Menu menu;

    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.view().cursor_row == 2);

    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.confirm = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.cycle_value() == 1);

    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.confirm = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.cycle_value() == 2);

    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    REQUIRE(menu.update(bumpy::MenuInput{.confirm = true}) == bumpy::MenuAction::none);
    REQUIRE(menu.cycle_value() == 0);
}

TEST_CASE("menu state starts the first level from the first playable selection") {
    bumpy::Menu menu;

    REQUIRE(menu.update(bumpy::MenuInput{.confirm = true}) ==
            bumpy::MenuAction::start_first_level);
}

TEST_CASE("menu confirms high scores from the second selection") {
    bumpy::Menu menu;

    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);  // row 1
    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);              // release
    REQUIRE(menu.view().cursor_row == 1);
    REQUIRE(menu.update(bumpy::MenuInput{.confirm = true}) == bumpy::MenuAction::high_scores);
}

TEST_CASE("menu state emits quit from the fourth confirmed selection") {
    bumpy::Menu menu;

    for (int row = 0; row < 3; ++row) {
        REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);
        REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);
    }

    REQUIRE(menu.view().cursor_row == 3);
    REQUIRE(menu.update(bumpy::MenuInput{.confirm = true}) == bumpy::MenuAction::quit);
}

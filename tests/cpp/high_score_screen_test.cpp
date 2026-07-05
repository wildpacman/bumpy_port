#include <catch2/catch_test_macros.hpp>

#include "game/high_score_screen.h"
#include "game/high_scores.h"

using bumpy::HighScoreMode;
using bumpy::HighScoreResult;
using bumpy::HighScoreScreen;
using bumpy::HighScoreTable;
using bumpy::MenuInput;

TEST_CASE("view mode dismisses on any key, but only after the entry key releases") {
    HighScoreScreen s;
    s.enter_view();
    REQUIRE(s.view().mode == HighScoreMode::view);
    REQUIRE(s.view().insert_row == -1);

    // Confirm still held from the menu: must not dismiss until released.
    REQUIRE(s.update(MenuInput{.confirm = true}) == HighScoreResult::none);
    REQUIRE(s.update(MenuInput{}) == HighScoreResult::none);                 // release clears guard
    REQUIRE(s.update(MenuInput{.confirm = true}) == HighScoreResult::done);  // fresh press dismisses
}

TEST_CASE("entry mode with a qualifying score opens the editor at column 0") {
    HighScoreTable table;
    HighScoreScreen s;
    s.enter_entry(table, 2000000);  // beats STEVE -> inserted at row 2, name AAAAAAAA
    REQUIRE(s.view().mode == HighScoreMode::entry);
    REQUIRE(s.view().insert_row == 2);
    REQUIRE(s.view().cursor_col == 0);
    REQUIRE(table.entry(2).name[0] == 'A');
}

TEST_CASE("up/down cycle the caret glyph; left/right move the caret 0..7") {
    HighScoreTable table;
    HighScoreScreen s;
    s.enter_entry(table, 2000000);
    REQUIRE(s.update(MenuInput{}) == HighScoreResult::none);  // release guard

    // DOWN once: 'A' -> 'B' at column 0 (DOWN advances toward '.').
    REQUIRE(s.update(MenuInput{.down = true}) == HighScoreResult::none);
    REQUIRE(table.entry(2).name[0] == 'B');
    for (int i = 0; i < 20; ++i) s.update(MenuInput{});  // let the repeat delay expire
    s.update(MenuInput{.up = true});                     // 'B' -> 'A'
    for (int i = 0; i < 20; ++i) s.update(MenuInput{});
    s.update(MenuInput{.up = true});                     // 'A' -> '9' (UP advances toward '0')
    REQUIRE(table.entry(2).name[0] == '9');

    // Right to column 1 (release between actions so the repeat delay does not swallow it).
    for (int i = 0; i < 20; ++i) s.update(MenuInput{});  // let the repeat delay expire
    REQUIRE(s.update(MenuInput{.right = true}) == HighScoreResult::none);
    REQUIRE(s.view().cursor_col == 1);

    // Left cannot go below 0.
    for (int i = 0; i < 20; ++i) s.update(MenuInput{});
    s.update(MenuInput{.left = true});
    for (int i = 0; i < 20; ++i) s.update(MenuInput{});
    s.update(MenuInput{.left = true});
    REQUIRE(s.view().cursor_col == 0);
}

TEST_CASE("fire confirms and finishes the editor") {
    HighScoreTable table;
    HighScoreScreen s;
    s.enter_entry(table, 2000000);
    REQUIRE(s.update(MenuInput{}) == HighScoreResult::none);  // release guard
    REQUIRE(s.update(MenuInput{.confirm = true}) == HighScoreResult::done);
}

TEST_CASE("a game over that does not qualify shows the table and dismisses on any key") {
    HighScoreTable table;
    HighScoreScreen s;
    s.enter_entry(table, 100);  // below MIKE (500): no insert
    REQUIRE(s.view().mode == HighScoreMode::entry);
    REQUIRE(s.view().insert_row == -1);
    REQUIRE(s.update(MenuInput{}) == HighScoreResult::none);                 // release guard
    REQUIRE(s.update(MenuInput{.confirm = true}) == HighScoreResult::done);
}

#include <catch2/catch_test_macros.hpp>

#include "game/app.h"
#include "game/level_game.h"  // LevelStatus

namespace {

// Tick the App until the world-map fire jump (cloud-jump animation) finishes and the
// level screen is reached.
void finish_jump(bumpy::App& app) {
    int guard = 0;
    while (app.world_map().is_jumping() && guard++ < 1000) {
        REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    }
}

// Drive the App from the menu to the level the way a player would: confirm "start"
// (menu -> world map), release, then fire on node 1 (map -> jump animation -> level
// board 0), release.
void enter_level(bumpy::App& app) {
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);  // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    finish_jump(app);  // play the cloud-jump before the board loads
    REQUIRE(app.screen() == bumpy::Screen::level);
    REQUIRE(app.board_index() == 0);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);  // release
}

// Tick the App until the world-map avatar finishes any in-progress slide.
void finish_slide(bumpy::App& app) {
    int guard = 0;
    while (app.world_map().is_sliding() && guard++ < 1000) {
        REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    }
}

// Tick the App with no input until it leaves the timed GAME OVER screen.
void pass_game_over(bumpy::App& app) {
    int guard = 0;
    while (app.screen() == bumpy::Screen::game_over && guard++ < 1000) {
        REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    }
}

}  // namespace

TEST_CASE("app starts on the menu at board zero") {
    const bumpy::App app(15);

    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.board_index() == 0);
    REQUIRE(app.board_count() == 15);
}

TEST_CASE("app latches the LEVEL difficulty chosen in the menu and then resets it") {
    bumpy::App app(15);
    REQUIRE(app.difficulty() == 0);          // EASY default
    REQUIRE(app.level_pattern() == 0xff);

    // Move to row 2 (LEVEL) and cycle once to MEDIUM.
    REQUIRE(app.update(bumpy::MenuInput{.down = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.down = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.menu().view().cursor_row == 2);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.menu().cycle_value() == 1);   // MEDIUM

    // Back up to PLAY and start the run.
    REQUIRE(app.update(bumpy::MenuInput{.up = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.up = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.menu().view().cursor_row == 0);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);

    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE(app.difficulty() == 1);           // MEDIUM latched for the whole run
    REQUIRE(app.level_pattern() == 0xaa);
    REQUIRE(app.menu().cycle_value() == 0);   // the menu resets to EASY for the next visit
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
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);    // slide to node 2
    finish_slide(app);                                                                     // glide there
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // fire
    finish_jump(app);                                                                      // cloud-jump
    REQUIRE(app.screen() == bumpy::Screen::level);
    REQUIRE(app.board_index() == 1);  // node 2 -> board 1
}

TEST_CASE("cancel on the world map is a game over, then the menu") {
    // FUN_1000_3852 Escape sets DAT_928d = 0xff -> FUN_1000_0c18 runs the timed GAME OVER
    // flash (11eb) then `goto LAB_0c2c` (menu), WITHOUT the high-score table -- so map
    // Escape drops the run: game_over -> menu.
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::game_over);
    pass_game_over(app);
    REQUIRE(app.screen() == bumpy::Screen::menu);  // straight to the menu, no high-score table
}

TEST_CASE("re-entering the map resets the avatar to node 1") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);    // slide to node 2
    finish_slide(app);                                                                     // glide there
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);   // -> game over
    pass_game_over(app);                                                                   // -> menu
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.world_map().current_node() == 1);
}

TEST_CASE("the App does not treat in-level cancel as a jump to the menu") {
    // In-level Escape is owned by LevelGame (scancode 0x01 -> FUN_1000_22fc, lose a life;
    // see level_game_test), NOT by the App. App::update must therefore leave the level
    // screen alone on cancel -- the old "level -> menu" jump discarded the whole run.
    bumpy::App app(15);
    enter_level(app);

    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::level);
}

TEST_CASE("cancel on the menu quits") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("quitting from the menu uses Escape, not a menu row") {
    bumpy::App app(15);

    // Escape on the menu quits (the original menu has no quit row; the port adds it on cancel).
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("the fourth menu row opens the password screen") {
    bumpy::App app(15);

    for (int row = 0; row < 3; ++row) {
        REQUIRE(app.update(bumpy::MenuInput{.down = true}) == bumpy::AppOutcome::running);
        REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    }
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::password);
}

namespace {

// Hold down until the caret cell reaches `target` (held-repeat cycles the glyph). Codes are
// letters (>= the 'A' seed), reached by DOWN (A -> B -> ...); UP would walk toward the digits.
void app_set_cell(bumpy::App& app, char target) {
    auto cell = [&] {
        const auto& v = app.password_screen().view();
        return v.code[static_cast<std::size_t>(v.cursor_col)];
    };
    for (int guard = 0; guard < 400 && cell() != target; ++guard) {
        app.update(bumpy::MenuInput{.down = true});
    }
}

void app_move_right(bumpy::App& app) {
    for (int i = 0; i < 10; ++i) app.update(bumpy::MenuInput{});  // let the repeat delay expire
    app.update(bumpy::MenuInput{.right = true});
}

}  // namespace

TEST_CASE("a valid password sets the world the next PLAY starts at") {
    bumpy::App app(15);

    // Menu row 3 -> password screen.
    for (int row = 0; row < 3; ++row) {
        app.update(bumpy::MenuInput{.down = true});
        app.update(bumpy::MenuInput{});
    }
    app.update(bumpy::MenuInput{.confirm = true});
    REQUIRE(app.screen() == bumpy::Screen::password);
    app.update(bumpy::MenuInput{});  // release the opening confirm

    // Spell ACCESS (world 2) and commit.
    const char code[] = "ACCESS";
    for (int col = 0; col < 6; ++col) {
        app_set_cell(app, code[col]);
        if (col < 5) app_move_right(app);
    }
    for (int i = 0; i < 10; ++i) app.update(bumpy::MenuInput{});  // let the repeat delay expire
    app.update(bumpy::MenuInput{.confirm = true});  // commit

    // Run out the result flash; the screen returns to the menu with world 2 selected.
    for (int i = 0; i < 120 && app.screen() == bumpy::Screen::password; ++i) {
        app.update(bumpy::MenuInput{});
    }
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.selected_world() == 2);

    // Navigate up to PLAY and start: the run targets world 2, so the shell is asked to load it.
    app.update(bumpy::MenuInput{});
    while (app.menu().view().cursor_row > 0) {
        app.update(bumpy::MenuInput{.up = true});
        app.update(bumpy::MenuInput{});
    }
    app.update(bumpy::MenuInput{.confirm = true});
    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE(app.pending_world() == 2);       // shell must load world 2
    REQUIRE(app.selected_world() == 1);      // selection consumed back to the default
}

TEST_CASE("a held confirm does not bounce menu -> map -> level") {
    bumpy::App app(15);

    // First confirm edge: menu -> map.
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);

    // Still holding confirm: must not select a board until the key is released.
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);

    // Release, then a fresh confirm edge plays the jump and enters the level.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    finish_jump(app);
    REQUIRE(app.screen() == bumpy::Screen::level);
}

TEST_CASE("a held cancel does not bounce map -> game over -> menu -> quit") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release

    // First cancel edge: map -> game over. Then, holding cancel the whole time (the flash
    // advances on a frame count, ignoring input), it must never quit -- not through the
    // GAME OVER flash, and not on arrival at the menu (a held Escape is release-guarded).
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::game_over);
    int guard = 0;
    while (app.screen() != bumpy::Screen::menu && guard++ < 1000) {
        REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    }
    REQUIRE(app.screen() == bumpy::Screen::menu);
    // Still holding cancel on the menu: must not quit until released.
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);

    // Release, then a fresh cancel edge quits from the menu.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("winning a board marks it cleared and carries lives/score to the map") {
    bumpy::App app(15);
    enter_level(app);  // board 0

    app.finish_level(bumpy::LevelStatus::won, 4, 1250);

    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE(app.is_board_cleared(0));
    REQUIRE(app.lives() == 4);
    REQUIRE(app.score() == 1250);
}

TEST_CASE("dying returns to the map without clearing the board (replayable)") {
    bumpy::App app(15);
    enter_level(app);

    app.finish_level(bumpy::LevelStatus::dead, 3, 500);

    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE_FALSE(app.is_board_cleared(0));
    REQUIRE(app.lives() == 3);  // LevelGame already decremented the life
    REQUIRE(app.score() == 500);
}

TEST_CASE("running out of lives goes to game over then high scores, then resets the run") {
    bumpy::App app(15);
    enter_level(app);

    app.finish_level(bumpy::LevelStatus::quit, 0, 9999);  // 22fc set 928d=0xff
    REQUIRE(app.screen() == bumpy::Screen::game_over);
    pass_game_over(app);
    REQUIRE(app.screen() == bumpy::Screen::high_scores);

    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // dismiss
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.lives() == 5);  // run reset
    REQUIRE(app.score() == 0);
    REQUIRE_FALSE(app.is_board_cleared(0));
}

TEST_CASE("menu row 1 opens the high-score table in view mode and returns to the menu") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.down = true}) == bumpy::AppOutcome::running);   // row 1
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);               // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::high_scores);
    REQUIRE(app.high_score_screen().view().mode == bumpy::HighScoreMode::view);

    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);               // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
}

TEST_CASE("game over shows GAME OVER, then the high-score table with the run's score") {
    bumpy::App app(15);
    enter_level(app);

    app.finish_level(bumpy::LevelStatus::quit, 0, 9999);  // out of lives
    REQUIRE(app.screen() == bumpy::Screen::game_over);

    pass_game_over(app);
    REQUIRE(app.screen() == bumpy::Screen::high_scores);
    REQUIRE(app.high_score_screen().view().mode == bumpy::HighScoreMode::entry);
    REQUIRE(app.high_score_screen().view().insert_row >= 0);  // 9999 beats MIKE (500)

    // Release the (absent) key, then fire commits the name; the run resets, returns to menu.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.lives() == 5);   // run reset after the high-score screen
    REQUIRE(app.score() == 0);
}

TEST_CASE("clearing every board in a non-final world requests the next world") {
    bumpy::App app(1);  // a one-board world 1: clearing board 0 finishes it
    enter_level(app);

    app.finish_level(bumpy::LevelStatus::won, 5, 7000);

    REQUIRE(app.is_board_cleared(0));
    REQUIRE(app.screen() == bumpy::Screen::map);  // returns to the map, not the menu
    REQUIRE(app.pending_world() == 2);            // shell must load world 2

    // The shell satisfies the request (world 2, also one board for the test).
    app.enter_world(2, 1);
    REQUIRE(app.world() == 2);
    REQUIRE(app.pending_world() == 0);
    REQUIRE(app.world_map().world() == 2);
    REQUIRE(app.world_map().current_node() == 1);  // back to the start node
    REQUIRE_FALSE(app.is_board_cleared(0));         // fresh world, progress reset
}

TEST_CASE("starting a new game reloads the world (clears progress)") {
    bumpy::App app(15);
    enter_level(app);
    app.finish_level(bumpy::LevelStatus::won, 4, 1250);  // board 0 cleared, on the map
    REQUIRE(app.is_board_cleared(0));
    REQUIRE(app.screen() == bumpy::Screen::map);

    // Escape off the map is a game over (release first -- the map guards fire/cancel):
    // map -> game_over -> menu, and the run resets on the way. Then start again:
    // start_first_level reloads the world (FUN_1000_2d14), wiping progress + score/lives.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);   // map -> game over
    pass_game_over(app);                                                                   // -> menu
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // start
    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE_FALSE(app.is_board_cleared(0));
    REQUIRE(app.lives() == 5);
    REQUIRE(app.score() == 0);
}

TEST_CASE("left/right do nothing on the menu screen") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.left = true, .right = true}) ==
            bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.board_index() == 0);
}

TEST_CASE("App can start on a configured world") {
    bumpy::App app(12, 4);
    REQUIRE(app.world() == 4);
    REQUIRE(app.world_map().world() == 4);
    REQUIRE(app.board_count() == 12);
    REQUIRE(app.pending_world() == 0);
}

TEST_CASE("clearing the final world shows the ending screen instead of the menu") {
    bumpy::App app(1, 9);  // a one-board world 9
    enter_level(app);

    app.finish_level(bumpy::LevelStatus::won, 4, 12345);

    REQUIRE(app.screen() == bumpy::Screen::outro);  // DESSFIN.VEC ending (FUN_1000_3ed4)
}

TEST_CASE("the ending screen waits for release, then a key resets the run and returns to the menu") {
    bumpy::App app(1, 9);  // a one-board world 9
    enter_level(app);
    app.finish_level(bumpy::LevelStatus::won, 4, 12345);
    REQUIRE(app.screen() == bumpy::Screen::outro);

    // The key that won the board is still held: it must not dismiss the ending (FUN_1000_328f
    // clears the latch and waits for a *fresh* press).
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::outro);

    // Release, then a fresh press dismisses to the menu and resets the run (DAT_79b2 = 1).
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.pending_world() == 0);  // start world == 9 stays loaded; no reload
    REQUIRE(app.score() == 0);          // run reset
    REQUIRE(app.lives() == 5);
}

TEST_CASE("dismissing the ending screen with Escape does not also quit the menu") {
    bumpy::App app(1, 9);  // a one-board world 9
    enter_level(app);
    app.finish_level(bumpy::LevelStatus::won, 5, 1);
    REQUIRE(app.screen() == bumpy::Screen::outro);

    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);  // release winning key
    // A fresh Escape dismisses the ending to the menu -- and must not carry through to a quit.
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
    // Still holding Escape: guarded until release (no bounce into a menu quit).
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
}

TEST_CASE("a game over after advancing requests a reload of the start world") {
    bumpy::App app(1);    // start world 1
    enter_level(app);
    app.finish_level(bumpy::LevelStatus::won, 5, 100);  // clear world 1 -> pending world 2
    app.enter_world(2, 1);                               // shell loads world 2
    REQUIRE(app.world() == 2);

    enter_level(app);                                    // play a world-2 board
    app.finish_level(bumpy::LevelStatus::quit, 0, 200);  // out of lives -> game over (200 < 500)
    REQUIRE(app.screen() == bumpy::Screen::game_over);
    pass_game_over(app);
    REQUIRE(app.screen() == bumpy::Screen::high_scores);
    REQUIRE(app.high_score_screen().view().insert_row == -1);  // 200 does not qualify
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // dismiss

    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.pending_world() == 1);  // reset_run asks the shell to reload world 1
    app.enter_world(1, 15);
    REQUIRE(app.world() == 1);
    REQUIRE(app.lives() == 5);
    REQUIRE(app.score() == 0);
}

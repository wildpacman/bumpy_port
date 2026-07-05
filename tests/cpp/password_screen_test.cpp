#include <catch2/catch_test_macros.hpp>

#include "game/password_screen.h"

#include <array>

using bumpy::MenuInput;
using bumpy::PasswordResult;
using bumpy::PasswordScreen;

namespace {

std::array<char, 6> code_of(const char* s) {
    return {s[0], s[1], s[2], s[3], s[4], s[5]};
}

// Let the held-repeat delay expire between editor actions.
void idle(PasswordScreen& s, int frames = 10) {
    for (int i = 0; i < frames; ++i) {
        s.update(MenuInput{});
    }
}

// Cycle the current caret cell to `target`. Codes are letters (>= the 'A' seed), reached by
// pressing DOWN (A -> B -> ... -> Z); UP would walk toward the digits instead.
void set_cell(PasswordScreen& s, char target) {
    for (int guard = 0; guard < 40 && s.view().code[static_cast<std::size_t>(s.view().cursor_col)] != target;
         ++guard) {
        s.update(MenuInput{.down = true});
        idle(s);
    }
}

void move_right(PasswordScreen& s) {
    s.update(MenuInput{.right = true});
    idle(s);
}

// Type a full 6-letter code from the default AAAAAA and commit it.
int enter_code(PasswordScreen& s, const char* code) {
    s.enter();
    s.update(MenuInput{});  // clear the release guard
    for (int col = 0; col < 6; ++col) {
        set_cell(s, code[col]);
        if (col < 5) {
            move_right(s);
        }
    }
    s.update(MenuInput{.confirm = true});  // commit
    // Run out the result flash.
    for (int i = 0; i < 60; ++i) {
        if (s.update(MenuInput{}) == PasswordResult::done) {
            break;
        }
    }
    return s.matched_world();
}

}  // namespace

TEST_CASE("password_world matches all eight world codes and rejects others") {
    REQUIRE(bumpy::password_world(code_of("ACCESS")) == 2);
    REQUIRE(bumpy::password_world(code_of("BUTTON")) == 3);
    REQUIRE(bumpy::password_world(code_of("ISLAND")) == 4);
    REQUIRE(bumpy::password_world(code_of("PRETTY")) == 5);
    REQUIRE(bumpy::password_world(code_of("WINNER")) == 6);
    REQUIRE(bumpy::password_world(code_of("ZOMBIE")) == 7);
    REQUIRE(bumpy::password_world(code_of("LOVELY")) == 8);
    REQUIRE(bumpy::password_world(code_of("SYSTEM")) == 9);
    REQUIRE(bumpy::password_world(code_of("AAAAAA")) == 0);
    REQUIRE(bumpy::password_world(code_of("ACCES.")) == 0);
}

TEST_CASE("password screen seeds AAAAAA at column 0") {
    PasswordScreen s;
    s.enter();
    REQUIRE(s.view().code == code_of("AAAAAA"));
    REQUIRE(s.view().cursor_col == 0);
    REQUIRE_FALSE(s.view().showing_result);
}

TEST_CASE("password screen swallows the opening confirm until released") {
    PasswordScreen s;
    s.enter();
    REQUIRE(s.update(MenuInput{.confirm = true}) == PasswordResult::none);  // still held from menu
    REQUIRE_FALSE(s.view().showing_result);
    REQUIRE(s.update(MenuInput{}) == PasswordResult::none);  // release clears the guard
    REQUIRE(s.update(MenuInput{.confirm = true}) == PasswordResult::none);  // fresh fire commits
    REQUIRE(s.view().showing_result);
}

TEST_CASE("down/up cycle the caret glyph the right way; left/right move the caret within 0..5") {
    PasswordScreen s;
    s.enter();
    s.update(MenuInput{});  // clear guard

    // DOWN advances toward '.' ('A' -> 'B'); UP advances toward '0' ('A' -> '9').
    REQUIRE(s.update(MenuInput{.down = true}) == PasswordResult::none);
    REQUIRE(s.view().code[0] == 'B');
    idle(s);
    s.update(MenuInput{.up = true});  // 'B' -> 'A'
    idle(s);
    s.update(MenuInput{.up = true});  // 'A' -> '9'
    REQUIRE(s.view().code[0] == '9');

    idle(s);
    move_right(s);
    REQUIRE(s.view().cursor_col == 1);

    // Left cannot go below 0.
    idle(s);
    s.update(MenuInput{.left = true});
    idle(s);
    s.update(MenuInput{.left = true});
    REQUIRE(s.view().cursor_col == 0);

    // Right cannot go past 5.
    for (int i = 0; i < 8; ++i) {
        idle(s);
        s.update(MenuInput{.right = true});
    }
    REQUIRE(s.view().cursor_col == 5);
}

TEST_CASE("glyph cycle clamps: UP floors at '0', DOWN ceils at '.'") {
    PasswordScreen s;
    s.enter();
    s.update(MenuInput{});  // clear guard
    // UP from the 'A' seed walks 9,8,...,0 and stops at '0' (no wrap).
    for (int i = 0; i < 20; ++i) {
        s.update(MenuInput{.up = true});
        idle(s);
    }
    REQUIRE(s.view().code[0] == '0');
    // DOWN from '0' walks up through the digits and letters to '.', and stops at '.'.
    for (int i = 0; i < 60; ++i) {
        s.update(MenuInput{.down = true});
        idle(s);
    }
    REQUIRE(s.view().code[0] == '.');
}

TEST_CASE("a short tap steps once; holding auto-repeats only after an initial delay") {
    // A brief tap (held a few frames, then released) advances exactly one glyph.
    PasswordScreen s;
    s.enter();
    s.update(MenuInput{});  // clear guard
    for (int i = 0; i < 4; ++i) s.update(MenuInput{.down = true});
    REQUIRE(s.view().code[0] == 'B');  // 'A' -> 'B', one step
    s.update(MenuInput{});             // release
    REQUIRE(s.view().code[0] == 'B');  // still one step -- no runaway

    // Holding does NOT auto-repeat within the initial-delay window: 18 held frames -> still one
    // step; the 19th produces the second. (Then the cadence is a fast 8 frames -- see the video.)
    PasswordScreen h;
    h.enter();
    h.update(MenuInput{});  // clear guard
    for (int i = 0; i < 18; ++i) h.update(MenuInput{.down = true});
    REQUIRE(h.view().code[0] == 'B');  // one step so far (initial delay not yet elapsed)
    h.update(MenuInput{.down = true});
    REQUIRE(h.view().code[0] == 'C');  // second step after the initial delay
}

TEST_CASE("a valid code commits, flashes OK, and reports the world") {
    PasswordScreen s;
    REQUIRE(enter_code(s, "ACCESS") == 2);
    REQUIRE(s.view().result_ok);
}

TEST_CASE("an invalid code commits, flashes ERROR, and reports world 0") {
    PasswordScreen s;
    s.enter();
    s.update(MenuInput{});                 // clear guard
    s.update(MenuInput{.confirm = true});  // commit the default AAAAAA (invalid)
    REQUIRE(s.view().showing_result);
    REQUIRE_FALSE(s.view().result_ok);
    PasswordResult r = PasswordResult::none;
    for (int i = 0; i < 60 && r == PasswordResult::none; ++i) {
        r = s.update(MenuInput{});
    }
    REQUIRE(r == PasswordResult::done);
    REQUIRE(s.matched_world() == 0);
}

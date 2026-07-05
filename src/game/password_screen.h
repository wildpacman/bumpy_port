#pragma once

#include "game/menu.h"  // MenuInput

#include <array>
#include <cstddef>

namespace bumpy {

enum class PasswordResult { none, done };

struct PasswordScreenView {
    std::array<char, 6> code{{'A', 'A', 'A', 'A', 'A', 'A'}};  // current entry, for the renderer
    int cursor_col{0};                                        // caret column 0..5
    bool caret_visible{true};                                 // blink state of the caret cell
    bool showing_result{false};                               // result message phase (post-commit)
    bool result_ok{false};                                    // true = "PASSWORD OK", false = ERROR
};

// The PASSWORD menu sub-screen (FUN_1000_0f7a + the editor FUN_1000_5c87), delegated to by
// App like HighScoreScreen. Fill 6 cells with 'A'; UP/DOWN cycle the glyph, LEFT/RIGHT move
// the caret over all 6 columns, fire commits (no cancel -- the original's 5c87 only leaves on
// fire, like the high-score name entry). On commit the 6 chars are matched against the 8
// baked passwords (worlds 2-9); a brief result flash follows, then the screen is done.
// See analysis/specs/menu-behavior.md ("Password screen").
class PasswordScreen {
public:
    void enter() noexcept;  // reset to AAAAAA, entry mode

    // Advance one frame. Returns done once the post-commit result flash has elapsed.
    PasswordResult update(const MenuInput& input) noexcept;

    [[nodiscard]] const PasswordScreenView& view() const noexcept { return view_; }

    // The matched world (2..9), or 0 if the entered code matched no password. Valid after
    // update() has returned done (and stable throughout the result flash).
    [[nodiscard]] int matched_world() const noexcept { return matched_world_; }

private:
    void cycle_glyph(int direction) noexcept;
    void commit() noexcept;  // validate the entry -> matched_world_ + result phase

    PasswordScreenView view_{};
    int matched_world_{0};
    bool waiting_for_release_{true};  // swallow the menu-confirm that opened the screen
    int repeat_{0};                   // frames until the next held-repeat action
    bool first_step_{true};           // next action is a fresh press (takes the initial delay)
    int blink_{0};                    // caret blink counter
    int result_frames_{0};            // frames the result message has been shown
};

// Match a 6-character code against the 8 baked passwords; returns the world (2..9) or 0.
[[nodiscard]] int password_world(const std::array<char, 6>& code) noexcept;

// Return the baked 6-character password for a world (2..9), or AAAAAA outside that range.
[[nodiscard]] std::array<char, 6> password_code_for_world(int world) noexcept;

}  // namespace bumpy

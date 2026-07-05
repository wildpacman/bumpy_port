#include "game/password_screen.h"

#include <string_view>

namespace bumpy {
namespace {

// The 8 passwords for worlds 2-9 (DS:0x135c, table[i] -> world i+2). Confirmed from the
// data segment; see analysis/specs/menu-behavior.md ("Password screen").
constexpr std::string_view kPasswords[8] = {
    "ACCESS",  // world 2
    "BUTTON",  // world 3
    "ISLAND",  // world 4
    "PRETTY",  // world 5
    "WINNER",  // world 6
    "ZOMBIE",  // world 7
    "LOVELY",  // world 8
    "SYSTEM",  // world 9
};

// The glyph cycle for one cell (FUN_1000_5c87, twin of the high-score name editor). The editor
// steps a sprite-frame index over the CONTIGUOUS run 0x1ac..0x1d0 = glyphs '0'-'9','A'-'Z','.'
// (verified by dumping the frames). It is CLAMPED, not wrapped: UP decrements the frame and
// floors at '0' (0x1ac); DOWN increments and ceils at '.' (0x1d0). Seed 'A'. So from 'A':
// UP -> 9,8,...,0 (stop), DOWN -> B,...,Z,. (stop). All passwords are letters.
constexpr char kCycle[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.";
constexpr int kCycleLen = 37;

// Pacing. The cursor blinks 8 frames on / 8 off (FUN_1000_5c87's `local_8 & 8`, confirmed
// against the original capture screenshots/bumpy_000.avi). The held auto-repeat is 8 frames/step
// (also confirmed from the capture -- a steady 8-frame cadence while a key is held). A tap or a
// brief hold does a SINGLE step: the first repeat waits a longer initial delay (typematic feel),
// so the player can land on one glyph without overshooting -- an intentional playability nicety
// over the original's constant 8-frame rate.
constexpr int kHoldRepeatFrames = 7;      // 8-frame cadence for sustained holds (7 + the action)
constexpr int kInitialRepeatDelay = 17;   // ~18-frame delay before the FIRST auto-repeat
constexpr int kBlinkFrames = 8;           // caret blink half-period (8 on / 8 off)
constexpr int kResultFrames = 45;         // result flash (~0.6 s at 70 Hz)

int cycle_index(char c) noexcept {
    for (int i = 0; i < kCycleLen; ++i) {
        if (kCycle[i] == c) {
            return i;
        }
    }
    return 10;  // 'A' -- the seeded default (index 10 in "0123456789A...")
}

bool any_key(const MenuInput& in) noexcept {
    return in.up || in.down || in.left || in.right || in.confirm || in.cancel;
}

}  // namespace

int password_world(const std::array<char, 6>& code) noexcept {
    for (int i = 0; i < 8; ++i) {
        if (std::string_view(code.data(), code.size()) == kPasswords[i]) {
            return i + 2;  // password i -> world i+2
        }
    }
    return 0;
}

std::array<char, 6> password_code_for_world(int world) noexcept {
    std::array<char, 6> code{{'A', 'A', 'A', 'A', 'A', 'A'}};
    if (world < 2 || world > 9) {
        return code;
    }
    const std::string_view password = kPasswords[world - 2];
    for (std::size_t i = 0; i < code.size(); ++i) {
        code[i] = password[i];
    }
    return code;
}

void PasswordScreen::enter() noexcept {
    view_ = {};
    matched_world_ = 0;
    waiting_for_release_ = true;
    repeat_ = 0;
    first_step_ = true;
    blink_ = 0;
    result_frames_ = 0;
}

void PasswordScreen::cycle_glyph(int direction) noexcept {
    char& c = view_.code[static_cast<std::size_t>(view_.cursor_col)];
    int idx = cycle_index(c) + direction;  // CLAMP (no wrap): floor '0', ceil '.'
    if (idx < 0) {
        idx = 0;
    } else if (idx >= kCycleLen) {
        idx = kCycleLen - 1;
    }
    c = kCycle[idx];
}

void PasswordScreen::commit() noexcept {
    matched_world_ = password_world(view_.code);  // 0 if no match
    view_.showing_result = true;
    view_.result_ok = matched_world_ >= 2;
    result_frames_ = 0;
}

PasswordResult PasswordScreen::update(const MenuInput& input) noexcept {
    // Release guard: swallow the menu-confirm that opened the screen before accepting a
    // fire as the entry commit (mirrors the high-score editor's fresh-press wait).
    if (waiting_for_release_) {
        if (!any_key(input)) {
            waiting_for_release_ = false;
        }
        return PasswordResult::none;
    }

    // Result flash (post-commit): show " PASSWORD OK " / "PASSWORD ERROR" briefly, then done.
    if (view_.showing_result) {
        if (++result_frames_ >= kResultFrames) {
            return PasswordResult::done;
        }
        return PasswordResult::none;
    }

    // Entry mode: blink the caret cell.
    if (++blink_ >= kBlinkFrames) {
        blink_ = 0;
        view_.caret_visible = !view_.caret_visible;
    }

    // Fire commits the entry (FUN_1000_5c87 leaves only on bit 0x10); cancel is ignored.
    if (input.confirm) {
        commit();
        return PasswordResult::none;
    }

    // Held-repeat editing: cycle the glyph (up/down) or move the caret (left/right). Releasing
    // every direction key re-arms a fresh press to act immediately and take the initial delay.
    const bool any_dir = input.up || input.down || input.left || input.right;
    if (!any_dir) {
        first_step_ = true;
        repeat_ = 0;
    }
    if (repeat_ > 0) {
        --repeat_;
        return PasswordResult::none;
    }
    bool acted = false;
    if (input.up) {
        cycle_glyph(-1);  // UP -> toward '0' (9,8,...,0), floors at '0' (FUN_1000_5c87)
        acted = true;
    } else if (input.down) {
        cycle_glyph(+1);  // DOWN -> toward '.' (B,...,Z,.), ceils at '.'
        acted = true;
    } else if (input.left) {
        if (view_.cursor_col > 0) {
            --view_.cursor_col;
            acted = true;
        }
    } else if (input.right) {
        if (view_.cursor_col < 5) {
            ++view_.cursor_col;
            acted = true;
        }
    }
    if (acted) {
        repeat_ = first_step_ ? kInitialRepeatDelay : kHoldRepeatFrames;
        first_step_ = false;
        view_.caret_visible = true;
        blink_ = 0;
    }
    return PasswordResult::none;
}

}  // namespace bumpy

#include "game/high_score_screen.h"

#include "game/high_scores.h"

namespace bumpy {
namespace {

// The name-entry glyph cycle (FUN_1000_59d3, twin of the password editor FUN_1000_5c87). It
// steps a sprite-frame index over the CONTIGUOUS run 0x1ac..0x1d0 = glyphs '0'-'9','A'-'Z','.'
// (verified by dumping the frames), CLAMPED not wrapped: UP decrements and floors at '0'
// (0x1ac); DOWN increments and ceils at '.' (0x1d0). Seed 'A'.
constexpr char kNameCycle[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.";
constexpr int kNameCycleLen = 37;  // number of chars in kNameCycle (excludes the NUL)

// Pacing (matches the password editor -- both are twins of 59d3/5c87; 8-frame blink and hold
// cadence confirmed from the original capture, with an added initial delay so a tap = one step).
constexpr int kHoldRepeatFrames = 7;     // 8-frame cadence for sustained holds
constexpr int kInitialRepeatDelay = 17;  // ~18-frame delay before the FIRST auto-repeat
constexpr int kCaretBlinkFrames = 8;     // caret blink half-period (8 on / 8 off)

int cycle_index(char c) noexcept {
    for (int i = 0; i < kNameCycleLen; ++i) {
        if (kNameCycle[i] == c) {
            return i;
        }
    }
    return 10;  // 'A' -- the seeded default
}

bool any_key(const MenuInput& in) noexcept {
    return in.up || in.down || in.left || in.right || in.confirm || in.cancel;
}

}  // namespace

void HighScoreScreen::enter_view() noexcept {
    view_ = {};
    view_.mode = HighScoreMode::view;
    view_.insert_row = -1;
    table_ = nullptr;
    waiting_for_release_ = true;
    repeat_ = 0;
    first_step_ = true;
    blink_ = 0;
}

void HighScoreScreen::enter_entry(HighScoreTable& table, std::uint32_t score) noexcept {
    view_ = {};
    view_.mode = HighScoreMode::entry;
    view_.insert_row = table.insert(score);  // -1 if it did not qualify
    view_.cursor_col = 0;
    view_.caret_visible = true;
    table_ = &table;
    waiting_for_release_ = true;
    repeat_ = 0;
    first_step_ = true;
    blink_ = 0;
}

void HighScoreScreen::cycle_glyph(int direction) noexcept {
    if (table_ == nullptr || view_.insert_row < 0) {
        return;
    }
    char& c = table_->entry(static_cast<std::size_t>(view_.insert_row))
                  .name[static_cast<std::size_t>(view_.cursor_col)];
    int idx = cycle_index(c) + direction;  // CLAMP (no wrap): floor '0', ceil '.'
    if (idx < 0) {
        idx = 0;
    } else if (idx >= kNameCycleLen) {
        idx = kNameCycleLen - 1;
    }
    c = kNameCycle[idx];
}

HighScoreResult HighScoreScreen::update(const MenuInput& input) noexcept {
    // Release guard: the key that opened the screen (menu confirm, or the death) must be
    // released before any dismissal / edit is accepted (mirrors 328f's fresh-press wait and
    // avoids 59d3's held-fire instant-commit quirk).
    if (waiting_for_release_) {
        if (!any_key(input)) {
            waiting_for_release_ = false;
        }
        return HighScoreResult::none;
    }

    // Blink the caret cell every kCaretBlinkFrames frames.
    if (++blink_ >= kCaretBlinkFrames) {
        blink_ = 0;
        view_.caret_visible = !view_.caret_visible;
    }

    // View mode, or a game over that did not qualify: any key dismisses (FUN_1000_328f).
    if (view_.insert_row < 0) {
        return any_key(input) ? HighScoreResult::done : HighScoreResult::none;
    }

    // Entry mode with a caret: held-repeat editing; fire commits (FUN_1000_59d3). Cancel is
    // ignored -- the original leaves 59d3 only on fire.
    if (input.confirm) {
        return HighScoreResult::done;
    }
    const bool any_dir = input.up || input.down || input.left || input.right;
    if (!any_dir) {
        first_step_ = true;
        repeat_ = 0;
    }
    if (repeat_ > 0) {
        --repeat_;
        return HighScoreResult::none;
    }
    bool acted = false;
    if (input.up) {
        cycle_glyph(-1);  // UP -> toward '0' (9,8,...,0), floors at '0' (FUN_1000_59d3)
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
        if (view_.cursor_col < 7) {
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
    return HighScoreResult::none;
}

}  // namespace bumpy

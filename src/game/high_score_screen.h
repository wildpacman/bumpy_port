#pragma once

#include "game/menu.h"  // MenuInput

#include <cstdint>

namespace bumpy {

class HighScoreTable;

enum class HighScoreMode { view, entry };
enum class HighScoreResult { none, done };

struct HighScoreScreenView {
    HighScoreMode mode{HighScoreMode::view};
    int insert_row{-1};        // caret row (0..6) in entry mode, else -1 (view / no qualify)
    int cursor_col{0};         // caret column 0..7
    bool caret_visible{true};  // blink state for the caret cell
};

// The transient HIGH-SCORE screen state, delegated to by App like WorldMap. Recovered
// from FUN_1000_57e1 (view / any-key dismiss via 328f) and FUN_1000_59d3 (held-repeat
// name editor: up/down cycle the glyph, left/right move the caret across all 8 columns,
// fire commits). No debounce on the editor (the original re-polls the held key each
// iteration) -- held keys repeat, gated by a per-action frame delay.
class HighScoreScreen {
public:
    // From the menu: read-only table, any key returns to the menu (score is 0 -> nothing
    // qualifies in the original).
    void enter_view() noexcept;

    // From Game Over: insert `score` into `table` if it qualifies (driving the caret over
    // the inserted row) or leave it read-only; either way, mode = entry.
    void enter_entry(HighScoreTable& table, std::uint32_t score) noexcept;

    // Advance one frame. Edits the inserted row's name in place during entry mode.
    HighScoreResult update(const MenuInput& input) noexcept;

    [[nodiscard]] const HighScoreScreenView& view() const noexcept { return view_; }

private:
    void cycle_glyph(int direction) noexcept;

    HighScoreScreenView view_{};
    HighScoreTable* table_{};         // the table being edited (entry mode); null in view mode
    bool waiting_for_release_{true};  // swallow the key that opened the screen
    int repeat_{0};                   // frames until the next held-repeat action
    bool first_step_{true};           // next action is a fresh press (takes the initial delay)
    int blink_{0};                    // caret blink counter
};

}  // namespace bumpy

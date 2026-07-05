#pragma once

#include "core/indexed_framebuffer.h"
#include "game/high_score_screen.h"  // HighScoreScreenView
#include "game/high_scores.h"

#include <cstdint>
#include <span>

namespace bumpy {

// Map a name/score character to its BUMSPJEU sprite-glyph frame, or -1 for a blank cell
// ('.' / space). Recovered sheet: '0'-'9' = 0x1ac..0x1b5, 'A'-'Z' = 0x1b6..0x1cf,
// '[' (caret) = 0x1d0. (FUN_1000_57e1 / FUN_1000_942a(0x792e), glyphs origin (0,0).)
[[nodiscard]] int high_score_glyph_frame(char c) noexcept;

// The GAME OVER screen (FUN_1000_11eb): SCORE.VEC backdrop + "GAME OVER" glyph text at
// column 6, y = 96. `score_vec` is the decoded 320x200 screen-format SCORE.VEC.
void render_game_over(std::span<const std::uint8_t> score_vec,
                      std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);

// The HIGH-SCORE table (FUN_1000_5681 -> 57e1): SCORE.VEC backdrop + 7 rows of name glyphs
// (x = col*16, y = row*16 + 65) and 7-digit scores (x = 176 + i*16). On view.insert_row the
// caret column draws '[' while view.caret_visible.
void render_high_scores(std::span<const std::uint8_t> score_vec, const HighScoreTable& table,
                        std::span<const std::uint8_t> sprite_bank,
                        const HighScoreScreenView& view, IndexedFramebuffer& target);

}  // namespace bumpy

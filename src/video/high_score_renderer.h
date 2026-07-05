#pragma once

#include "core/indexed_framebuffer.h"
#include "game/high_score_screen.h"  // HighScoreScreenView
#include "game/high_scores.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bumpy {

// Map a name/score character to its BUMSPJEU sprite-glyph frame, or -1 for a blank cell.
// Recovered sheet (verified by dumping the bank): '0'-'9' = 0x1ac..0x1b5, 'A'-'Z' =
// 0x1b6..0x1cf, '.' = 0x1d0. This is the *display* mapping: '.' -> -1 (blank), because a
// stored '.' in a shown name row is a space (FUN_1000_57e1). Origin (0,0).
[[nodiscard]] int high_score_glyph_frame(char c) noexcept;

// The *editor* mapping: identical, except a '.' cell shows the '.' glyph (0x1d0) -- used for
// the password field and the high-score row being edited (57e1 maps the edit row's '.' to it).
[[nodiscard]] int editor_glyph_frame(char c) noexcept;

// Draw a run of glyph cells left-to-right from (x, y), stepping 16 px; blank chars are skipped.
// draw_glyph_string uses the display mapping (shared by high-score / GAME OVER / PASSWORD text);
// draw_editor_glyphs uses the editor mapping (the password entry field, where '.' shows).
void draw_glyph_string(const char* text, std::size_t len, int x, int y,
                       std::span<const std::uint8_t> bank, IndexedFramebuffer& target);
void draw_editor_glyphs(const char* text, std::size_t len, int x, int y,
                        std::span<const std::uint8_t> bank, IndexedFramebuffer& target);

// The GAME OVER screen (FUN_1000_11eb): SCORE.VEC backdrop + "GAME OVER" glyph text at
// column 6, y = 96. `score_vec` is the decoded 320x200 screen-format SCORE.VEC.
void render_game_over(std::span<const std::uint8_t> score_vec,
                      std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);

// The HIGH-SCORE table (FUN_1000_5681 -> 57e1): SCORE.VEC backdrop + 7 rows of name glyphs
// (x = col*16, y = row*16 + 65) and 7-digit scores (x = 176 + i*16). On view.insert_row the
// caret column blinks its glyph (hidden in the blink-off phase), and a '.' cell shows the '.' glyph.
void render_high_scores(std::span<const std::uint8_t> score_vec, const HighScoreTable& table,
                        std::span<const std::uint8_t> sprite_bank,
                        const HighScoreScreenView& view, IndexedFramebuffer& target);

}  // namespace bumpy

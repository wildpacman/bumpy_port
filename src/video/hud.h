#pragma once

#include "core/indexed_framebuffer.h"
#include "resources/font.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace bumpy {

// The lives indicator: BUMSPJEU frame 0x1aa repeated once per remaining life
// (FUN_1000_6130).
inline constexpr int kLifeIconFrame = 0x1aa;
// The first life icon's descriptor x is life_index*8 + 0x50; icons step 8px right.
inline constexpr int kLifeIconBaseX = 0x50;

// The score is always FUN_1000_0816's 7 zero-padded decimal digits.
inline constexpr int kScoreDigits = 7;
// World-map score cursor: FUN_1000_0816(score, ..., 7, 1, 8) -- raw pixels, where the
// Y is the glyph baseline (the top scanline is baseline - font.ascent()).
inline constexpr int kMapScoreX = 1;
inline constexpr int kMapScoreBaselineY = 8;
// The score glyph colour. The text rasterizer draws set bits in DAT_693e; on the world
// map it resolves to palette index 14 -- matching the original score colour in
// screenshots/bumpy_001.png exactly (MONDE1 pal[14] = 162,162,130, a light olive-gray).
inline constexpr std::uint8_t kScoreColor = 14;

// Draw the remaining-lives row at the top of the screen, exactly as FUN_1000_6130:
// for life index i = lives..1 the icon's descriptor is (i*8 + 0x50, 0) and the
// overlay blitter centres a frame on its descriptor by half its dimensions. Drawn on
// both the world map and the in-level playfield. Frames that fail to decode are skipped.
void draw_lives(std::span<const std::uint8_t> sprite_bank, std::uint8_t lives,
                IndexedFramebuffer& target);

// Draw `score` as kScoreDigits zero-padded decimal digits using the DDFNT2.CAR font,
// exactly as FUN_1000_0816: cursor_x is the first digit's left pixel, baseline_y is the
// glyph baseline (top scanline = baseline_y - font.ascent()), set bits drawn in `color`,
// each digit advancing by glyph.width + font.spacing(). Used on the world map.
void draw_score(const Font& font, std::uint32_t score, int cursor_x, int baseline_y,
                std::uint8_t color, IndexedFramebuffer& target);

// Total advance width in pixels of `text` in `font` (sum of per-glyph width + spacing,
// including the trailing spacing) -- for right-aligning a string before draw_text.
[[nodiscard]] int measure_text(const Font& font, std::string_view text);

// Draw `text` left-to-right from (x, baseline_y) in `font`, set bits in `color`. Generalises
// draw_score to an arbitrary string; characters outside the font advance as blank cells.
void draw_text(const Font& font, std::string_view text, int x, int baseline_y,
               std::uint8_t color, IndexedFramebuffer& target);

// The port's TAB->OPTIONS discoverability hint (a port addition, not in the original), drawn
// small and right-aligned in the bottom-right corner of the title menu so a first-time player
// learns the settings overlay exists. Uses the small HUD font (DDFNT2).
void draw_tab_hint(const Font& font, IndexedFramebuffer& target);

}  // namespace bumpy

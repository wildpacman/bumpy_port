#include "video/hud.h"

#include "resources/sprite_frame.h"
#include "video/menu_renderer.h"  // MenuImage

#include <exception>

namespace bumpy {
namespace {

// Blit a decoded sprite frame at the given top-left, clipped to the target, colour
// index 0 (sprite_transparent_index) skipped. (Same convention as the map/board blits.)
void blit_sprite(const MenuImage& image, int top_x, int top_y, IndexedFramebuffer& target) {
    for (int py = 0; py < image.height; ++py) {
        const int ty = top_y + py;
        if (ty < 0 || ty >= target.height()) {
            continue;
        }
        for (int px = 0; px < image.width; ++px) {
            const int tx = top_x + px;
            if (tx < 0 || tx >= target.width()) {
                continue;
            }
            const auto color = image.pixels[static_cast<std::size_t>(py) * image.width + px];
            if (color != sprite_transparent_index) {
                target.pixel(tx, ty) = color;
            }
        }
    }
}

}  // namespace

void draw_lives(std::span<const std::uint8_t> sprite_bank, std::uint8_t lives,
                IndexedFramebuffer& target) {
    if (lives == 0) {
        return;
    }
    try {
        const MenuImage icon = decode_sprite_frame(sprite_bank, kLifeIconFrame);
        for (int i = lives; i > 0; --i) {
            const int descriptor_x = i * 8 + kLifeIconBaseX;  // FUN_1000_6130
            blit_sprite(icon, descriptor_x - icon.width / 2, 0, target);
        }
    } catch (const std::exception&) {
        // life icon frame unavailable -> skip the lives row
    }
}

void draw_text(const Font& font, std::string_view text, int x, int baseline_y,
               std::uint8_t color, IndexedFramebuffer& target) {
    const int top_baseline = baseline_y - font.ascent();
    for (const char ch : text) {
        const FontGlyph g = font.glyph(static_cast<unsigned char>(ch));
        const int top = top_baseline + g.y_offset;
        for (int row = 0; row < g.height; ++row) {
            const int ty = top + row;
            if (ty < 0 || ty >= target.height()) {
                continue;
            }
            for (int col = 0; col < g.width; ++col) {
                const auto byte = g.bitmap[static_cast<std::size_t>(row) * g.bytes_per_row + col / 8];
                if (((byte >> (7 - col % 8)) & 1) == 0) {
                    continue;
                }
                const int tx = x + col;
                if (tx >= 0 && tx < target.width()) {
                    target.pixel(tx, ty) = color;
                }
            }
        }
        x += g.width + font.spacing();
    }
}

int measure_text(const Font& font, std::string_view text) {
    int width = 0;
    for (const char ch : text) {
        width += font.glyph(static_cast<unsigned char>(ch)).width + font.spacing();
    }
    return width;
}

void draw_score(const Font& font, std::uint32_t score, int cursor_x, int baseline_y,
                std::uint8_t color, IndexedFramebuffer& target) {
    // FUN_1000_0816 fills all kScoreDigits positions with a digit, so the score is
    // zero-padded (no leading spaces).
    char digits[kScoreDigits];
    std::uint32_t value = score;
    for (int i = kScoreDigits - 1; i >= 0; --i) {
        digits[i] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    draw_text(font, std::string_view(digits, kScoreDigits), cursor_x, baseline_y, color, target);
}

void draw_tab_hint(const Font& font, IndexedFramebuffer& target) {
    // A port addition, not in the original title screen. Right-aligned in the bottom-right
    // corner in TITRE palette idx 14 (pale gold, 227,195,97) -- the lightest, most legible
    // colour there, warm like the BUMPY'S logo and clearly not one of the green menu rows.
    static constexpr std::string_view kHint = "TAB - OPTIONS";
    constexpr std::uint8_t kColor = 14;
    constexpr int kRightX = 313;      // text right edge (~8px from the 320 frame edge)
    constexpr int kBaselineY = 195;   // glyphs span ~188..195 (a few px above the bottom)
    const int x = kRightX - measure_text(font, kHint);
    draw_text(font, kHint, x, kBaselineY, kColor, target);
}

}  // namespace bumpy

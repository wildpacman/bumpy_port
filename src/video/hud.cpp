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

    const int top_baseline = baseline_y - font.ascent();
    int x = cursor_x;
    for (const char digit : digits) {
        const FontGlyph g = font.glyph(static_cast<unsigned char>(digit));
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

}  // namespace bumpy

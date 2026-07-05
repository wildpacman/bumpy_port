#include "video/high_score_renderer.h"

#include "resources/sprite_frame.h"  // decode_sprite_frame, sprite_transparent_index
#include "video/menu_renderer.h"     // MenuImage
#include "video/screen_image.h"

#include <cstddef>
#include <exception>

namespace bumpy {
namespace {

// Layout, recovered from FUN_1000_57e1 / FUN_1000_11eb.
constexpr int kRowStepY = 16;
constexpr int kNameX0 = 0;
constexpr int kNameRowY0 = 0x41;     // 65
constexpr int kScoreX0 = 0xb0;       // 176
constexpr int kGlyphStepX = 16;
constexpr int kScoreDigits = 7;
constexpr int kGameOverX0 = 6 * 16;  // column 6
constexpr int kGameOverY = 0x60;     // 96
constexpr char kCaretGlyph = '[';

// Blit one decoded glyph frame with its top-left at (x, y) -- glyph frames have origin
// (0,0), so no hotspot offset. Colour index 0 (sprite_transparent_index) is skipped.
void blit_glyph(int frame_index, int x, int y, std::span<const std::uint8_t> bank,
                IndexedFramebuffer& target) {
    try {
        const MenuImage g = decode_sprite_frame(bank, frame_index);
        for (int py = 0; py < g.height; ++py) {
            const int ty = y + py;
            if (ty < 0 || ty >= target.height()) continue;
            for (int px = 0; px < g.width; ++px) {
                const int tx = x + px;
                if (tx < 0 || tx >= target.width()) continue;
                const auto color = g.pixels[static_cast<std::size_t>(py) * g.width + px];
                if (color != sprite_transparent_index) target.pixel(tx, ty) = color;
            }
        }
    } catch (const std::exception&) {
        // an undecodable glyph frame is skipped (blank cell)
    }
}

// Draw a string of glyph cells left-to-right, stepping kGlyphStepX; blank chars skipped.
void draw_glyph_string(const char* text, std::size_t len, int x, int y,
                       std::span<const std::uint8_t> bank, IndexedFramebuffer& target) {
    for (std::size_t i = 0; i < len; ++i) {
        const int frame = high_score_glyph_frame(text[i]);
        if (frame >= 0) blit_glyph(frame, x, y, bank, target);
        x += kGlyphStepX;
    }
}

void draw_background(std::span<const std::uint8_t> score_vec, IndexedFramebuffer& target) {
    if (is_screen_image(score_vec)) {
        apply_screen_image_palette(score_vec, target);
        draw_screen_image(score_vec, target);
    }
}

}  // namespace

int high_score_glyph_frame(char c) noexcept {
    if (c >= '0' && c <= '9') return 0x1ac + (c - '0');
    if (c >= 'A' && c <= 'Z') return 0x1b6 + (c - 'A');
    if (c == kCaretGlyph) return 0x1d0;
    return -1;  // '.', ' ', anything else -> blank
}

void render_game_over(std::span<const std::uint8_t> score_vec,
                      std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target) {
    draw_background(score_vec, target);
    static constexpr char kText[] = "GAME OVER";  // DS:0x1327, 9 chars
    draw_glyph_string(kText, 9, kGameOverX0, kGameOverY, sprite_bank, target);
}

void render_high_scores(std::span<const std::uint8_t> score_vec, const HighScoreTable& table,
                        std::span<const std::uint8_t> sprite_bank,
                        const HighScoreScreenView& view, IndexedFramebuffer& target) {
    draw_background(score_vec, target);

    for (std::size_t row = 0; row < kHighScoreCount; ++row) {
        const int y = static_cast<int>(row) * kRowStepY + kNameRowY0;
        const auto& entry = table.entry(row);

        // Name cells. On the inserted row, the caret column shows '[' while it is visible.
        for (int col = 0; col < static_cast<int>(kHighScoreNameLength); ++col) {
            char c = entry.name[static_cast<std::size_t>(col)];
            if (static_cast<int>(row) == view.insert_row && col == view.cursor_col &&
                view.caret_visible) {
                c = kCaretGlyph;
            }
            const int frame = high_score_glyph_frame(c);
            if (frame >= 0) blit_glyph(frame, kNameX0 + col * kGlyphStepX, y, sprite_bank, target);
        }

        // Score: 7 zero-padded digits.
        char digits[kScoreDigits];
        std::uint32_t value = entry.score;
        for (int i = kScoreDigits - 1; i >= 0; --i) {
            digits[i] = static_cast<char>('0' + value % 10);
            value /= 10;
        }
        draw_glyph_string(digits, kScoreDigits, kScoreX0, y, sprite_bank, target);
    }
}

}  // namespace bumpy

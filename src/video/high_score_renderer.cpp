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

void draw_background(std::span<const std::uint8_t> score_vec, IndexedFramebuffer& target) {
    if (is_screen_image(score_vec)) {
        apply_screen_image_palette(score_vec, target);
        draw_screen_image(score_vec, target);
    }
}

}  // namespace

int high_score_glyph_frame(char c) noexcept {
    // Contiguous sprite-frame run (verified by dumping the bank): '0'-'9' = 0x1ac-0x1b5,
    // 'A'-'Z' = 0x1b6-0x1cf, '.' = 0x1d0. NOTE '.' returns -1 here: in a *displayed* name row
    // FUN_1000_57e1 turns a stored '.' into a space (blank) -- the '.' glyph only shows in the
    // row being edited (see editor_glyph_frame).
    if (c >= '0' && c <= '9') return 0x1ac + (c - '0');
    if (c >= 'A' && c <= 'Z') return 0x1b6 + (c - 'A');
    return -1;  // '.', ' ', or anything else -> blank
}

int editor_glyph_frame(char c) noexcept {
    // The editing cell (password field / high-score insert row): here a '.' IS drawn, as the
    // '.' glyph 0x1d0 (FUN_1000_57e1 maps a '.' in the edit row to 0x5b -> frame 0x1d0).
    if (c == '.') return 0x1d0;
    return high_score_glyph_frame(c);
}

void draw_glyph_string(const char* text, std::size_t len, int x, int y,
                       std::span<const std::uint8_t> bank, IndexedFramebuffer& target) {
    for (std::size_t i = 0; i < len; ++i) {
        const int frame = high_score_glyph_frame(text[i]);
        if (frame >= 0) blit_glyph(frame, x, y, bank, target);
        x += kGlyphStepX;
    }
}

void draw_editor_glyphs(const char* text, std::size_t len, int x, int y,
                        std::span<const std::uint8_t> bank, IndexedFramebuffer& target) {
    // Like draw_glyph_string, but this is an edit field so a '.' cell shows the '.' glyph.
    for (std::size_t i = 0; i < len; ++i) {
        const int frame = editor_glyph_frame(text[i]);
        if (frame >= 0) blit_glyph(frame, x, y, bank, target);
        x += kGlyphStepX;
    }
}

void render_game_over(std::span<const std::uint8_t> score_vec,
                      std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target) {
    // FUN_1000_11eb draws "GAME OVER" on a BLACK screen. Unlike the high-score screen
    // (FUN_1000_5681, which deplanes SCORE.VEC via FUN_1000_7b5a), 11eb never decodes the
    // image into the buffer -- only the text shows over the darkened background. We still
    // install SCORE.VEC's palette so the glyph colour resolves (index 0 = black), then clear
    // to black rather than drawing the HALL OF FAME art.
    if (is_screen_image(score_vec)) {
        apply_screen_image_palette(score_vec, target);
    }
    target.clear(0);
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

        // Name cells. On the inserted row the caret column BLINKS its glyph (skipped in the
        // blink-off phase, matching 5c87's black 7b4a rectangle), and a '.' cell shows the '.'
        // glyph; other rows render a stored '.' as a blank (57e1).
        const bool editing_row = static_cast<int>(row) == view.insert_row;
        for (int col = 0; col < static_cast<int>(kHighScoreNameLength); ++col) {
            const int cx = kNameX0 + col * kGlyphStepX;
            if (editing_row && col == view.cursor_col && !view.caret_visible) continue;
            const char c = entry.name[static_cast<std::size_t>(col)];
            const int frame = editing_row ? editor_glyph_frame(c) : high_score_glyph_frame(c);
            if (frame >= 0) blit_glyph(frame, cx, y, sprite_bank, target);
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

#include "video/password_renderer.h"

#include "video/high_score_renderer.h"  // draw_glyph_string
#include "video/screen_image.h"

#include <array>
#include <cstddef>

namespace bumpy {
namespace {

// Layout, recovered from FUN_1000_0f7a ([0] = x = col*16, [1] = y):
constexpr int kPromptX = 0;      // col 0
constexpr int kPromptY = 0x10;   // 16
constexpr int kEntryX = 7 * 16;  // cols 7..12
constexpr int kEntryY = 0xa0;    // 160
constexpr int kResultX = 3 * 16;  // cols 3..16
constexpr int kResultY = 0x60;   // 96
constexpr int kDisplayPromptX = 4 * 16;  // cols 4..16
constexpr int kDisplayPromptY = 0x50;    // 80
constexpr int kDisplayCodeX = 7 * 16;    // cols 7..12
constexpr int kDisplayCodeY = 0x70;      // 112
constexpr int kGlyphStepX = 16;

constexpr char kDisplayPrompt[] = "YOUR PASSWORD";       // DS:0x12e7, 13 chars
constexpr char kPrompt[] = "ENTER YOUR PASSWORD";  // DS:0x12f5, 19 chars
constexpr char kOk[] = " PASSWORD OK  ";           // DS:0x1309, 14 chars
constexpr char kError[] = "PASSWORD ERROR";        // DS:0x1318, 14 chars

}  // namespace

void render_password(std::span<const std::uint8_t> score_vec,
                     std::span<const std::uint8_t> sprite_bank,
                     const PasswordScreenView& view, IndexedFramebuffer& target) {
    // FUN_1000_0f7a renders on a BLACK screen, exactly like GAME OVER (FUN_1000_11eb): it
    // loads SCORE.VEC only to install its palette (index 0 = black, plus the glyph colours) --
    // it never deplanes/blits the image. The image-paint steps the real high-score screen
    // (FUN_1000_5681) has -- FUN_1000_7b5a (deplane) + FUN_1000_80bc (full-frame blit) -- are
    // absent from 0f7a/0d9d/11eb; the 3467 screen-darken leaves the page black and the
    // password text is drawn over it. (An earlier version drew the HALL OF FAME art here,
    // the same mistake that was corrected for GAME OVER.)
    if (is_screen_image(score_vec)) {
        apply_screen_image_palette(score_vec, target);
    }
    target.clear(0);

    draw_glyph_string(kPrompt, 19, kPromptX, kPromptY, sprite_bank, target);

    // The 6-cell entry field (an edit field, so a '.' cell shows the '.' glyph). The cursor
    // cell BLINKS its glyph: in the blink-off phase it is simply not drawn (the original erases
    // it with a black FUN_1000_7b4a rectangle -- on this black screen that just hides the glyph),
    // so the letter flashes on and off rather than showing a solid block.
    for (int i = 0; i < 6; ++i) {
        const bool cursor_off = !view.showing_result && i == view.cursor_col && !view.caret_visible;
        if (cursor_off) continue;
        draw_editor_glyphs(&view.code[static_cast<std::size_t>(i)], 1, kEntryX + i * kGlyphStepX,
                           kEntryY, sprite_bank, target);
    }

    // The post-commit result flash.
    if (view.showing_result) {
        const char* msg = view.result_ok ? kOk : kError;
        draw_glyph_string(msg, 14, kResultX, kResultY, sprite_bank, target);
    }
}

void render_password_display(std::span<const std::uint8_t> score_vec,
                             std::span<const std::uint8_t> sprite_bank,
                             const std::array<char, 6>& code, IndexedFramebuffer& target) {
    if (is_screen_image(score_vec)) {
        apply_screen_image_palette(score_vec, target);
    }
    target.clear(0);
    draw_glyph_string(kDisplayPrompt, 13, kDisplayPromptX, kDisplayPromptY, sprite_bank, target);
    draw_glyph_string(code.data(), code.size(), kDisplayCodeX, kDisplayCodeY, sprite_bank, target);
}

}  // namespace bumpy

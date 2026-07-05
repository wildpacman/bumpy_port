#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "game/high_score_screen.h"
#include "game/high_scores.h"
#include "resources/binary_reader.h"   // read_binary_file
#include "resources/menu_resources.h"  // decode_sprite_archive
#include "video/high_score_renderer.h"
#include "video/screen_image.h"        // is_screen_image

namespace {
int nonzero_pixels(bumpy::IndexedFramebuffer& f, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < f.width(); ++x)
            if (f.pixel(x, y) != 0) ++n;
    return n;
}
}  // namespace

TEST_CASE("glyph frames map digits, letters and the dot") {
    REQUIRE(bumpy::high_score_glyph_frame('0') == 0x1ac);
    REQUIRE(bumpy::high_score_glyph_frame('9') == 0x1b5);
    REQUIRE(bumpy::high_score_glyph_frame('A') == 0x1b6);
    REQUIRE(bumpy::high_score_glyph_frame('Z') == 0x1cf);
    REQUIRE(bumpy::high_score_glyph_frame(' ') == -1);  // blank
    // '.' is blank in a *displayed* name row, but the '.' glyph (0x1d0) in an edit field.
    REQUIRE(bumpy::high_score_glyph_frame('.') == -1);
    REQUIRE(bumpy::editor_glyph_frame('.') == 0x1d0);
    REQUIRE(bumpy::editor_glyph_frame('A') == 0x1b6);  // letters unchanged
}

TEST_CASE("render_high_scores draws the backdrop and glyph rows") {
    const auto score_vec = bumpy::read_binary_file("SCORE.VEC");  // raw screen image, not a VEC
    REQUIRE(bumpy::is_screen_image(score_vec));
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");

    bumpy::HighScoreTable table;
    bumpy::HighScoreScreenView view{};  // view mode, no caret
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_high_scores(score_vec, table, bank.bytes(), view, frame);

    // The first name row sits at y = 65 (0x41); glyphs are 14-16 px tall. Expect drawn
    // pixels in the name band.
    REQUIRE(nonzero_pixels(frame, 65, 81) > 50);
}

TEST_CASE("render_game_over draws GAME OVER text on a black background") {
    const auto score_vec = bumpy::read_binary_file("SCORE.VEC");  // raw screen image, not a VEC
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_game_over(score_vec, bank.bytes(), frame);
    REQUIRE(nonzero_pixels(frame, 96, 112) > 30);  // the text band (y=96) has glyphs
    // FUN_1000_11eb does NOT paint the SCORE.VEC image -- the rest of the screen stays black.
    REQUIRE(nonzero_pixels(frame, 0, 90) == 0);     // above the text (was the HALL OF FAME title)
    REQUIRE(nonzero_pixels(frame, 116, 200) == 0);  // below the text (was the dotted frame)
}

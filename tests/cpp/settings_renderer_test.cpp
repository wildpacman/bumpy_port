#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "resources/binary_reader.h"
#include "resources/sprite_frame.h"
#include "video/screen_image.h"
#include "video/settings_renderer.h"

namespace {
int nonzero_pixels(bumpy::IndexedFramebuffer& f, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < f.width(); ++x)
            if (f.pixel(x, y) != 0) ++n;
    return n;
}
// Same as nonzero_pixels but restricted to an x-range too, so a test can pin the value
// column specifically (e.g. to catch a label/value overlap regression at kValueX).
int nonzero_pixels_x(bumpy::IndexedFramebuffer& f, int x0, int x1, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (f.pixel(x, y) != 0) ++n;
    return n;
}
}  // namespace

TEST_CASE("SettingsRenderer draws the root page title and rows") {
    const auto score = bumpy::read_binary_file("SCORE.VEC");
    REQUIRE(bumpy::is_screen_image(score));
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto fleche = bumpy::read_binary_file("FLECHE.BIN");
    bumpy::SettingsRenderer renderer(score, bank.bytes(), fleche);

    bumpy::SettingsView view{};
    view.page = bumpy::SettingsPage::root;
    view.render3d_available = true;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);

    REQUIRE(nonzero_pixels(frame, 16, 32) > 20);    // OPTIONS title band
    REQUIRE(nonzero_pixels(frame, 64, 152) > 40);   // the four rows + cursor
    REQUIRE(nonzero_pixels(frame, 0, 16) == 0);     // clear above the title
}

TEST_CASE("SettingsRenderer draws the passwords page") {
    const auto score = bumpy::read_binary_file("SCORE.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto fleche = bumpy::read_binary_file("FLECHE.BIN");
    bumpy::SettingsRenderer renderer(score, bank.bytes(), fleche);

    bumpy::SettingsView view{};
    view.page = bumpy::SettingsPage::passwords;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);

    REQUIRE(nonzero_pixels(frame, 56, 160) > 60);   // 8 code rows
}

TEST_CASE("SettingsRenderer draws the video page with a non-overlapping value column") {
    const auto score = bumpy::read_binary_file("SCORE.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto fleche = bumpy::read_binary_file("FLECHE.BIN");
    bumpy::SettingsRenderer renderer(score, bank.bytes(), fleche);

    bumpy::SettingsView view{};
    view.page = bumpy::SettingsPage::video;
    view.render3d = true;
    view.square_pixels = true;  // aspect value "16.10" -- the longest value (5 glyphs)
    view.fullscreen = true;
    view.render3d_available = true;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);

    REQUIRE(nonzero_pixels(frame, 16, 32) > 20);    // VIDEO title band
    REQUIRE(nonzero_pixels(frame, 64, 140) > 60);   // the three rows + cursor

    // Layout: labels at x=48.. (the longest, "FULLSCREEN", ends its last glyph cell at 208);
    // values left-aligned at kValueX=224. So [208,224) is the label/value gap and must be
    // blank, and the value column [224,320) must carry ink.
    REQUIRE(nonzero_pixels_x(frame, 208, 224, 64, 140) == 0);  // gap: no label/value overlap
    REQUIRE(nonzero_pixels_x(frame, 224, 320, 64, 140) > 0);   // values render

    // "16.10" (5 glyphs) ends at x=304 -- a 16px right margin -- so no value touches the frame
    // edge. This fails at the old kValueX=240 (where "16.10" spanned 240..320, flush to the
    // 320 edge -- the "runs off the edge / looks skewed" report), and passes at 224.
    REQUIRE(nonzero_pixels_x(frame, 304, 320, 64, 140) == 0);  // right margin clear
}

TEST_CASE("SettingsRenderer draws the audio page with a non-overlapping value column") {
    const auto score = bumpy::read_binary_file("SCORE.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto fleche = bumpy::read_binary_file("FLECHE.BIN");
    bumpy::SettingsRenderer renderer(score, bank.bytes(), fleche);

    bumpy::SettingsView view{};
    view.page = bumpy::SettingsPage::audio;
    view.music = true;
    view.sfx = true;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);

    REQUIRE(nonzero_pixels(frame, 16, 32) > 20);    // AUDIO title band
    REQUIRE(nonzero_pixels(frame, 64, 116) > 40);   // the two rows + cursor

    // Values (ON/ON) render in the value column, left-aligned at kValueX=224.
    REQUIRE(nonzero_pixels_x(frame, 224, 320, 64, 116) > 0);
}

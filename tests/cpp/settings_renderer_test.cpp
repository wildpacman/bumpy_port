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
    view.square_pixels = false;
    view.fullscreen = true;
    view.render3d_available = true;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);

    REQUIRE(nonzero_pixels(frame, 16, 32) > 20);    // VIDEO title band
    REQUIRE(nonzero_pixels(frame, 64, 140) > 60);   // the three rows + cursor

    // Labels never reach x >= 224 (the longest label, "FULLSCREEN", ends its last glyph
    // cell at 208-223), so any ink at x >= 256 can only come from a value's 2nd/3rd glyph.
    // At the old buggy kValueX = 208 the longest value here ("4.3", 3 glyphs) only reaches
    // column 255, so this band would be entirely blank -- this assertion fails under the
    // regression and passes only once the value column is moved clear of the label.
    REQUIRE(nonzero_pixels_x(frame, 256, 320, 64, 140) > 0);
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

    // Value column (ON/ON) must render distinctly in the value column.
    REQUIRE(nonzero_pixels_x(frame, 240, 320, 64, 116) > 0);
}

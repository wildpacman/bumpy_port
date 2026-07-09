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

#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "resources/binary_reader.h"
#include "resources/menu_resources.h"
#include "video/password_renderer.h"
#include "video/screen_image.h"

#include <array>

namespace {

std::array<char, 6> code_of(const char* s) {
    return {s[0], s[1], s[2], s[3], s[4], s[5]};
}

int nonzero_pixels(bumpy::IndexedFramebuffer& f, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < f.width(); ++x) {
            if (f.pixel(x, y) != 0) ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("render_password_display draws the between-world password on black") {
    const auto score_vec = bumpy::read_binary_file("SCORE.VEC");
    REQUIRE(bumpy::is_screen_image(score_vec));
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");

    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_password_display(score_vec, bank.bytes(), code_of("ACCESS"), frame);

    REQUIRE(nonzero_pixels(frame, 80, 96) > 40);    // YOUR PASSWORD
    REQUIRE(nonzero_pixels(frame, 112, 128) > 20);  // ACCESS
    REQUIRE(nonzero_pixels(frame, 0, 72) == 0);
    REQUIRE(nonzero_pixels(frame, 136, 200) == 0);
}

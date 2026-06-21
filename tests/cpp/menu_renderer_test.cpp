#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "resources/menu_resources.h"
#include "video/menu_renderer.h"

#include <array>
#include <cstdint>

TEST_CASE("menu renderer clips signed blits and preserves draw order") {
    bumpy::IndexedFramebuffer frame(4, 3);
    frame.clear(0);

    const bumpy::MenuImage first{3, 2, {1, 2, 3, 4, 5, 6}};
    const bumpy::MenuImage second{2, 2, {7, 8, 9, 10}};

    bumpy::draw_menu_command(
        bumpy::MenuDrawCommand{first, 0, 0, 3, 2, -1, 1},
        frame);
    bumpy::draw_menu_command(
        bumpy::MenuDrawCommand{second, 0, 0, 2, 2, 1, 1},
        frame);

    REQUIRE(frame.pixels()[4] == 2);
    REQUIRE(frame.pixels()[5] == 7);
    REQUIRE(frame.pixels()[6] == 8);
    REQUIRE(frame.pixels()[9] == 9);
    REQUIRE(frame.pixels()[10] == 10);
}

TEST_CASE("menu renderer applies transparency and masks") {
    bumpy::IndexedFramebuffer frame(4, 2);
    frame.clear(3);

    const bumpy::MenuImage image{4, 2, {0, 1, 2, 0, 4, 5, 6, 7}};
    bumpy::MenuDrawCommand command{image, 0, 0, 4, 2, 0, 0};
    command.transparent_index = 0;
    command.mask = {1, 1, 0, 1, 1, 0, 1, 1};

    bumpy::draw_menu_command(command, frame);

    REQUIRE(frame.pixels()[0] == 3);
    REQUIRE(frame.pixels()[1] == 1);
    REQUIRE(frame.pixels()[2] == 3);
    REQUIRE(frame.pixels()[3] == 3);
    REQUIRE(frame.pixels()[4] == 4);
    REQUIRE(frame.pixels()[5] == 3);
    REQUIRE(frame.pixels()[6] == 6);
    REQUIRE(frame.pixels()[7] == 7);
}

TEST_CASE("menu renderer converts VGA DAC components to full 8-bit RGBA") {
    REQUIRE(bumpy::vga_dac_to_rgba_component(0) == 0);
    REQUIRE(bumpy::vga_dac_to_rgba_component(32) == 130);
    REQUIRE(bumpy::vga_dac_to_rgba_component(63) == 255);
}

TEST_CASE("menu renderer decodes the title into a non-blank 320x200 indexed frame") {
    const auto resources = bumpy::MenuResources::load_from(".");
    bumpy::MenuRenderer renderer(resources);
    bumpy::IndexedFramebuffer frame(320, 200);

    renderer.render(bumpy::MenuView{}, frame);

    const auto pixels = frame.pixels();
    REQUIRE(pixels.size() == 320U * 200U);
    bool any_different = false;
    for (const auto value : pixels) {
        if (value != pixels[0]) {
            any_different = true;
            break;
        }
    }
    REQUIRE(any_different);
}

TEST_CASE("menu renderer deplanes the title to the full 16-index range") {
    const auto resources = bumpy::MenuResources::load_from(".");
    bumpy::MenuRenderer renderer(resources);
    bumpy::IndexedFramebuffer frame(320, 200);

    renderer.render(bumpy::MenuView{}, frame);

    // The deplaned 4-plane screen uses every VGA colour index.
    std::array<bool, 16> seen{};
    for (const auto value : frame.pixels()) {
        REQUIRE(value < 16);
        seen[value] = true;
    }
    for (const auto present : seen) {
        REQUIRE(present);
    }
}

TEST_CASE("menu renderer installs the screen's own VGA palette") {
    const auto resources = bumpy::MenuResources::load_from(".");
    bumpy::MenuRenderer renderer(resources);
    bumpy::IndexedFramebuffer frame(320, 200);

    renderer.render(bumpy::MenuView{}, frame);

    const auto& palette = frame.palette();
    // Colour 0 is black and the title is colourful, so the palette is not uniform.
    REQUIRE(palette[0].r == 0);
    REQUIRE(palette[0].g == 0);
    REQUIRE(palette[0].b == 0);
    bool any_colour = false;
    for (int index = 1; index < 16; ++index) {
        if (palette[index].r != 0 || palette[index].g != 0 || palette[index].b != 0) {
            any_colour = true;
        }
    }
    REQUIRE(any_colour);
    // Recovered palette entry 1 is the darkest background blue (DAC 0,0,16 -> 0,0,65).
    REQUIRE(palette[1].r == 0);
    REQUIRE(palette[1].g == 0);
    REQUIRE(palette[1].b == 65);
}

TEST_CASE("menu renderer draws the marker at the selected row") {
    const auto resources = bumpy::MenuResources::load_from(".");
    bumpy::MenuRenderer renderer(resources);
    bumpy::IndexedFramebuffer row0(320, 200);
    bumpy::IndexedFramebuffer row1(320, 200);

    renderer.render(bumpy::MenuView{true, true, 0}, row0);
    renderer.render(bumpy::MenuView{true, true, 1}, row1);

    const auto first_marker_pixel_x = 52;
    const auto first_marker_pixel_y = 115;
    REQUIRE(row0.pixels()[first_marker_pixel_y * 320 + first_marker_pixel_x] == 14);
    REQUIRE(row1.pixels()[first_marker_pixel_y * 320 + first_marker_pixel_x] != 14);
    REQUIRE(row1.pixels()[(first_marker_pixel_y + 16) * 320 + first_marker_pixel_x] == 14);
}

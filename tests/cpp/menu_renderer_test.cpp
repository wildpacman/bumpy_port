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

TEST_CASE("menu renderer places the confirmed cursor marker at the selected row") {
    const auto resources = bumpy::MenuResources::load_from(".");
    bumpy::MenuRenderer renderer(resources);
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::MenuView view{};
    view.draw_title = false;
    view.draw_cursor_marker = true;
    view.cursor_row = 2;

    renderer.render(view, frame);

    const auto marker_y = 0x70 + 2 * 0x10 + 1;
    for (int x = 0; x < 6; ++x) {
        REQUIRE(frame.pixels()[static_cast<std::size_t>(marker_y * 320 + 0x30 + x)] == 0x55);
    }
}

#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"

TEST_CASE("indexed framebuffer converts palette entries exactly") {
    bumpy::IndexedFramebuffer frame(2, 1);
    frame.set_palette(7, {0x12, 0x34, 0x56, 0xff});
    frame.pixel(0, 0) = 7;
    frame.pixel(1, 0) = 7;

    const auto rgba = frame.to_rgba();

    REQUIRE(rgba == std::vector<std::uint32_t>{0xff563412, 0xff563412});
}

TEST_CASE("indexed framebuffer rejects coordinates outside either axis") {
    bumpy::IndexedFramebuffer frame(2, 2);

    REQUIRE_THROWS_AS(frame.pixel(2, 0), std::out_of_range);
    REQUIRE_THROWS_AS(frame.pixel(-1, 1), std::out_of_range);
}

TEST_CASE("indexed framebuffer exposes read-only indexed pixels and palette") {
    bumpy::IndexedFramebuffer frame(2, 2);
    frame.set_palette(3, {1, 2, 3, 255});
    frame.pixel(0, 0) = 3;
    frame.pixel(1, 0) = 7;

    const auto pixels = frame.pixels();
    const auto& palette = frame.palette();

    REQUIRE(pixels.size() == 4);
    REQUIRE(pixels[0] == 3);
    REQUIRE(pixels[1] == 7);
    REQUIRE(palette[3].r == 1);
    REQUIRE(palette[3].g == 2);
    REQUIRE(palette[3].b == 3);
    REQUIRE(palette[3].a == 255);
}

TEST_CASE("indexed framebuffer clears every indexed pixel") {
    bumpy::IndexedFramebuffer frame(3, 2);
    frame.pixel(0, 0) = 1;
    frame.pixel(2, 1) = 2;

    frame.clear(9);

    for (const auto pixel : frame.pixels()) {
        REQUIRE(pixel == 9);
    }
}

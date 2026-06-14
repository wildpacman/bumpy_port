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

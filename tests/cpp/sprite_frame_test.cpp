#include <catch2/catch_test_macros.hpp>

#include "resources/sprite_frame.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    std::vector<std::uint8_t> bytes;
    char byte{};
    while (input.get(byte)) {
        bytes.push_back(static_cast<std::uint8_t>(byte));
    }
    REQUIRE(input.eof());
    return bytes;
}

}  // namespace

TEST_CASE("sprite frame decoder reads the menu cursor from FLECHE frame zero") {
    const auto image = bumpy::decode_sprite_frame(read_file("FLECHE.BIN"), 0);

    REQUIRE(image.width == 16);
    REQUIRE(image.height == 16);
    REQUIRE(image.pixels.size() == 16U * 16U);

    constexpr std::array<std::string_view, 16> expected_mask{
        "................",
        "................",
        "................",
        "....#...........",
        "....##..........",
        "....###.........",
        "....####........",
        "....#####.......",
        "....######......",
        "....#####.......",
        "....####........",
        "....###.........",
        "....##..........",
        "....#...........",
        "................",
        "................",
    };

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const auto pixel = image.pixels[static_cast<std::size_t>(y * image.width + x)];
            if (expected_mask[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '#') {
                REQUIRE(pixel != bumpy::sprite_transparent_index);
            } else {
                REQUIRE(pixel == bumpy::sprite_transparent_index);
            }
        }
    }
    REQUIRE(image.pixels[3U * 16U + 4U] == 0x0e);
    REQUIRE(image.pixels[8U * 16U + 4U] == 0x0d);
    REQUIRE(image.pixels[8U * 16U + 9U] == 0x0e);
}

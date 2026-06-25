#include <catch2/catch_test_macros.hpp>

#include "resources/font.h"

#include <array>
#include <cstdint>

// Tests run from the project root, so DDFNT2.CAR loads by bare name.
TEST_CASE("the DDFNT2 font decodes the digit glyphs recovered from the binary") {
    const auto font = bumpy::Font::load("DDFNT2.CAR");

    REQUIRE(font.first_char() == 0x20);  // space
    REQUIRE(font.ascent() == 7);
    REQUIRE(font.spacing() == 1);

    // '0' is the 7x7 rounded zero: bytes 7c c6 c6 c6 c6 c6 7c (recovered, MSB-first).
    const auto zero = font.glyph('0');
    REQUIRE(zero.width == 7);
    REQUIRE(zero.height == 7);
    REQUIRE(zero.y_offset == 0);
    REQUIRE(zero.bytes_per_row == 1);
    const std::array<std::uint8_t, 7> zero_bytes{0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c};
    REQUIRE(static_cast<std::size_t>(zero.bitmap.size()) == zero_bytes.size());
    for (std::size_t i = 0; i < zero_bytes.size(); ++i) {
        REQUIRE(zero.bitmap[i] == zero_bytes[i]);
    }

    // Variable width: '1' is 4px, '6'/'9' are 8px.
    REQUIRE(font.glyph('1').width == 4);
    REQUIRE(font.glyph('6').width == 8);
    REQUIRE(font.glyph('9').width == 8);

    // Space is a 4-wide blank (no bitmap) -> advance only.
    const auto space = font.glyph(' ');
    REQUIRE(space.width == 4);
    REQUIRE(space.height == 0);
    REQUIRE(space.bitmap.empty());

    // Out-of-range characters return an empty glyph (the original skips them).
    REQUIRE(font.glyph(0x01).width == 0);
    REQUIRE(font.glyph(0x01).bitmap.empty());
}

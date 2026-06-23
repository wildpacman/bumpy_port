#include <catch2/catch_test_macros.hpp>

#include "resources/entity_sprites.h"
#include "resources/level_resources.h"
#include "resources/menu_resources.h"
#include "resources/sprite_frame.h"
#include "video/board_renderer.h"

#include <filesystem>

namespace {
// Tests run with the working directory at the project root, so BUMSPJEU.BIN and
// the D?.* level blobs are reachable by name.
const std::filesystem::path root = ".";
}  // namespace

TEST_CASE("entity layer tables resolve recovered frame indices") {
    // Layer A: empty code 0 draws nothing; the peg code 1 -> frame 0x40, y_offset 5
    // (DS:0x3d3a[1]=1 -> *DS:0x3d6a[1]={5,0x40}); code 4 is unused.
    REQUIRE_FALSE(bumpy::entity_layer_a_sprite(0).present());
    REQUIRE(bumpy::entity_layer_a_sprite(1).present());
    REQUIRE(bumpy::entity_layer_a_sprite(1).frame_index == 0x40);
    REQUIRE(bumpy::entity_layer_a_sprite(1).y_offset == 5);
    REQUIRE_FALSE(bumpy::entity_layer_a_sprite(4).present());

    // Layer C: collectible frame is value + 0x179.
    REQUIRE(bumpy::entity_layer_c_frame(0x1b) == 0x194);
    REQUIRE(bumpy::entity_layer_c_frame(0x29) == 0x1a2);

    // Layer A/B grid position table DS:0xf4 (x=col*40, y=24+row*32).
    REQUIRE(bumpy::entity_layer_ab_position(0, 0).x == 0);
    REQUIRE(bumpy::entity_layer_ab_position(0, 0).y == 24);
    REQUIRE(bumpy::entity_layer_ab_position(3, 2).x == 120);
    REQUIRE(bumpy::entity_layer_ab_position(3, 2).y == 88);
}

TEST_CASE("recovered entity frames decode from the uncompressed BUMSPJEU bank") {
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");

    // Peg (layer A code 1) is a 32x6 bumper frame.
    const auto peg = bumpy::decode_sprite_frame(bank.bytes(), bumpy::entity_layer_a_sprite(1).frame_index);
    REQUIRE(peg.width == 32);
    REQUIRE(peg.height == 6);

    // Layer C collectibles are 16x16 object frames.
    for (std::uint8_t code : {0x1b, 0x03, 0x17, 0x29, 0x0f, 0x0e}) {
        const auto frame = bumpy::decode_sprite_frame(bank.bytes(), bumpy::entity_layer_c_frame(code));
        REQUIRE(frame.width == 16);
        REQUIRE(frame.height == 16);
        // A real sprite has at least one opaque (non-transparent) pixel.
        bool any_opaque = false;
        for (auto p : frame.pixels) {
            any_opaque = any_opaque || p != bumpy::sprite_transparent_index;
        }
        REQUIRE(any_opaque);
    }
}

TEST_CASE("level 1 board 0 entities all draw from the bank without skips") {
    const auto level = bumpy::LevelResources::load(root, 1);
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");
    bumpy::IndexedFramebuffer frame(320, 200);

    const auto stats = bumpy::draw_bum_entities(level.bum_entities(0), bank.bytes(), frame);

    // D1 board 0 is the documented peg field + six collectibles, no layer B.
    REQUIRE(stats.layer_a == 27);
    REQUIRE(stats.layer_c == 6);
    REQUIRE(stats.skipped == 0);
}

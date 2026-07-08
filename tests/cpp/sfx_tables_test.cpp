#include <catch2/catch_test_macros.hpp>
#include "resources/sfx_tables.h"

TEST_CASE("SFX preset table matches the recovered 6e30 switch") {
    using namespace bumpy;
    REQUIRE(kSfxPresets[1].used);
    REQUIRE(kSfxPresets[1].kind == SweepKind::tone);
    REQUIRE(kSfxPresets[1].init_divisor == 1000);
    REQUIRE(kSfxPresets[1].steps == 0x1e);
    REQUIRE(kSfxPresets[1].rate_seed == 0x1c2);
    REQUIRE(kSfxPresets[0x0b].kind == SweepKind::noise);
    REQUIRE_FALSE(kSfxPresets[0x13].used);        // dead case
    REQUIRE_FALSE(kSfxPresets[0].used);           // id 0 unused
    // Tile maps are the right size and silent-by-default fits (0 = no sound).
    REQUIRE(sizeof(kSfxIdleRest) == 0x30);
    REQUIRE(sizeof(kSfxLayerBBlock) == 0x20);
}

TEST_CASE("SFX map lookups do not wrap out-of-range selectors") {
    using namespace bumpy;

    REQUIRE(sfx_idle_rest(0x01) == kSfxIdleRest[0x01]);
    REQUIRE(sfx_roll_bump(0x12) == kSfxRollBump[0x12]);
    REQUIRE(sfx_fall_route(0x21) == kSfxFallRoute[0x21]);
    REQUIRE(sfx_held_bump(0x1e) == kSfxHeldBump[0x1e]);
    REQUIRE(sfx_layer_b_block(0x10) == kSfxLayerBBlock[0x10]);
    REQUIRE(sfx_picture_block(0x12) == kSfxPictureBlock[0x12]);

    // The original tables are fixed-size data ranges. Selectors outside them are
    // not another valid tile/block type in the speaker profile, so the port must
    // keep them silent instead of masking them onto a real sound id.
    REQUIRE(sfx_idle_rest(0x30) == 0);
    REQUIRE(sfx_roll_bump(0x30) == 0);
    REQUIRE(sfx_fall_route(0x30) == 0);
    REQUIRE(sfx_held_bump(0x30) == 0);
    REQUIRE(sfx_layer_b_block(0x20) == 0);
    REQUIRE(sfx_picture_block(0x20) == 0);
    REQUIRE(sfx_picture_block(0x2e) == 0);
}

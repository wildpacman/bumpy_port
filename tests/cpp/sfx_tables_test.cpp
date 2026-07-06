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

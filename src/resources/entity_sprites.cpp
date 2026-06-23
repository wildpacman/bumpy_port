#include "resources/entity_sprites.h"

#include <array>

namespace bumpy {
namespace {

// Precomputed value -> {frame_index, y_offset} for layer A, resolved from the
// table 0x3d3a (value -> sprite_index) and the near-pointer record table 0x3d6a
// (sprite_index -> {y_offset, frame_index}) in BUMPY.UNPACKED.EXE. 0x3d6a is what
// FUN_1000_2a78 (setup draw) and FUN_1000_14e4 (bump animation) actually use; the
// raw EXE never references 0x37be. The records are only *coincidentally* sequential
// for low indices (so lanes/pegs were right), but diverge for e.g. the level-exit
// pit: tile 0x20 -> sprite 0x7f -> frame 0xbe (the hole + animated down-arrow),
// NOT the green coil 0xb6 a flat 0x37be read returns. Index 0 / absent codes map to
// entity_no_sprite.
constexpr std::array<EntitySpriteRef, 48> kLayerA{{
    {0xffff, 0}, {0x40, 5}, {0xcc, 5}, {0x46, 5}, {0xffff, 0}, {0x4f, 3}, {0x51, 3}, {0x53, 5},
    {0x5c, 5}, {0x65, 5}, {0x71, 5}, {0x3f, 0}, {0x84, 5}, {0x6e, 6}, {0x9b, 5}, {0x9f, 2},
    {0xa3, 5}, {0xa8, 4}, {0xb3, 1}, {0xb4, 0}, {0xc4, 0}, {0xca, 0}, {0xcb, 2}, {0x89, 2},
    {0x88, 2}, {0x8a, 5}, {0xd4, 5}, {0xd3, 5}, {0xd2, 5}, {0xd1, 5}, {0xd8, 4}, {0xdf, 2},
    {0xbe, 0}, {0xbd, 2}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0},
    {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0},
    {0xffff, 0}, {0xffff, 0},
}};

// Layer B, resolved from 0x4086 (value -> sprite_index) and 0x3ad2 records. The
// small frame indices reference a bank region not yet confirmed; decode
// defensively at the call site.
constexpr std::array<EntitySpriteRef, 32> kLayerB{{
    {0xffff, 0}, {0x00, 2}, {0x02, 2}, {0x0d, 2}, {0x17, 2}, {0x46, 2}, {0x21, 2}, {0x27, 2},
    {0x2e, 4}, {0x3d, 1}, {0x3e, 1}, {0x3f, 1}, {0x45, 2}, {0x4c, 2}, {0x5b, 2}, {0x5e, 2},
    {0x60, 2}, {0x62, 2}, {0x64, 8}, {0x68, 2}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0},
    {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0},
    {0xffff, 0}, {0xffff, 0},
}};

}  // namespace

EntitySpriteRef entity_layer_a_sprite(std::uint8_t value) {
    return value < kLayerA.size() ? kLayerA[value] : EntitySpriteRef{};
}

EntitySpriteRef entity_layer_b_sprite(std::uint8_t value) {
    return value < kLayerB.size() ? kLayerB[value] : EntitySpriteRef{};
}

CellPosition entity_layer_ab_position(int col, int row) {
    return CellPosition{col * 40, 24 + row * 32};
}

}  // namespace bumpy

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

// Layer B (blocks), resolved from 0x4086 (value -> sprite_index) and the 0x3ad2
// record table {frame, y_offset}. The frame_index here is the ABSOLUTE bank frame:
// the raw 0x3ad2 record frame plus the +0xf1 layer-B bias the original applies at
// submit (FUN_1000_17c7, line `*(pcVar3+10) + 0xf1`). Layer A's 0x3d6a records are
// already absolute, so kLayerA needs no bias. Layer-B sprites also draw at a DISTINCT
// position table DS:0x3f4 (entity_layer_b_position), not layer A's DS:0xf4.
// (Raw record frames before the bias were 0x00,0x02,0x0d,0x17,0x46,0x21,0x27,0x2e,
//  0x3d,0x3e,0x3f,0x45,0x4c,0x5b,0x5e,0x60,0x62,0x64,0x68.)
constexpr std::array<EntitySpriteRef, 32> kLayerB{{
    {0xffff, 0}, {0xf1, 2}, {0xf3, 2}, {0xfe, 2}, {0x108, 2}, {0x137, 2}, {0x112, 2}, {0x118, 2},
    {0x11f, 4}, {0x12e, 1}, {0x12f, 1}, {0x130, 1}, {0x136, 2}, {0x13d, 2}, {0x14c, 2}, {0x14f, 2},
    {0x151, 2}, {0x153, 2}, {0x155, 8}, {0x159, 2}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0},
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

CellPosition entity_layer_b_position(int col, int row) {
    // Layer B reads a DISTINCT position table DS:0x3f4 (FUN_1000_17c7), 48 (x,y) word
    // pairs: x = 32 + col*40, y = row*32. This is offset (+32 x, -24 y) from layer A's
    // DS:0xf4. The per-sprite y_offset is added on top by the caller.
    return CellPosition{32 + col * 40, row * 32};
}

}  // namespace bumpy

#include "resources/entity_sprites.h"

#include <array>

namespace bumpy {
namespace {

// Precomputed value -> {frame_index, count} for layer A, resolved from the static
// tables 0x3d3a (value -> sprite_index) and 0x37be ({count, frame_index} records)
// in BUMPY.UNPACKED.EXE. Index 0 and absent codes map to entity_no_sprite.
constexpr std::array<EntitySpriteRef, 48> kLayerA{{
    {0xffff, 0}, {0x40, 5}, {0xc4, 0}, {0x46, 5}, {0xffff, 0}, {0x4f, 3}, {0x51, 3}, {0x53, 5},
    {0x5c, 5}, {0x65, 5}, {0x71, 5}, {0x3f, 0}, {0x84, 5}, {0x6e, 6}, {0x9b, 5}, {0xe5, 2},
    {0xa1, 2}, {0xa6, 5}, {0xab, 0}, {0xac, 4}, {0xbc, 1}, {0xc2, 0}, {0xc3, 0}, {0x89, 2},
    {0x88, 2}, {0x8a, 5}, {0xcf, 5}, {0xcd, 5}, {0xcb, 2}, {0xc9, 0}, {0xef, 2}, {0xd3, 5},
    {0xb6, 2}, {0xb5, 1}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0}, {0xffff, 0},
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

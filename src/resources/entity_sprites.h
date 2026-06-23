#pragma once

#include "resources/level_resources.h"  // CellPosition

#include <cstdint>

namespace bumpy {

// Resolves a BUM entity-grid cell value to a sprite frame in the BUMSPJEU.BIN
// bank. The bank's master frame table is addressed directly by frame index:
// decode_sprite_frame(bumspjeu_bytes, frame_index) reads the big-endian 32-bit
// pointer at frame_index*4, relocates it by +0x800 and decodes the 12-byte
// header + planar pixels. The entity sprites are all uncompressed (flags 0x0003);
// the compressed path (flags 0x40/0x20) is recovered but unused by the supplied
// assets. See analysis/specs/level-formats.md ("Entity sprite bank") and
// analysis/specs/menu-resource-formats.md.
//
// Selection chains recovered from FUN_1000_2a78 / FUN_1000_165e and the static
// descriptor tables in BUMPY.UNPACKED.EXE (data segment 0x103b):
//   Layer A: value -> 0x3d3a[value] = sprite_index -> *DS:0x3d6a[sprite_index] =
//            {y_offset, frame_index} (a near-pointer table, NOT a flat array -- the
//            records are non-sequential, e.g. the exit pit at sprite 0x7e/0x7f).
//   Layer B: value -> 0x4086[value] = sprite_index -> record at DS:0x3ad2; the
//            stored frame_index is ABSOLUTE (raw record frame + the +0xf1 layer-B
//            submit bias, FUN_1000_17c7). Layer-B draws at DS:0x3f4, not DS:0xf4.
//   Layer C: frame_index = value + 0x179 (collectibles).
// The {frame_index, count} records below are precomputed from those tables. The
// blitter adds `count` to the layer-A/B draw Y (DAT_8884[1] in FUN_1000_165e/17c7).

inline constexpr std::uint16_t entity_no_sprite = 0xffff;
inline constexpr std::uint16_t layer_c_frame_base = 0x179;

struct EntitySpriteRef {
    std::uint16_t frame_index{entity_no_sprite};  // entity_no_sprite => nothing to draw
    std::uint8_t y_offset{};                       // added to the draw Y (record `count`)
    [[nodiscard]] bool present() const noexcept { return frame_index != entity_no_sprite; }
};

// Layer A (pegs/bumpers): value indexes 0x3d3a (48 entries, codes 0..47).
[[nodiscard]] EntitySpriteRef entity_layer_a_sprite(std::uint8_t value);
// Layer B (second layer): value indexes 0x4086 (32 entries, codes 0..31). Its
// frame indices are small and reference a region of the bank not yet pinned, so
// callers should decode defensively and skip frames that do not resolve.
[[nodiscard]] EntitySpriteRef entity_layer_b_sprite(std::uint8_t value);
// Layer C (collectibles): the frame index is value + 0x179.
[[nodiscard]] inline std::uint16_t entity_layer_c_frame(std::uint8_t value) {
    return static_cast<std::uint16_t>(value + layer_c_frame_base);
}

// Draw position of a layer-A grid cell (DS:0xf4): x = col*40, y = 24 + row*32.
// The per-sprite y_offset is added on top by the caller. Distinct from the
// layer-C position table (DS:0x274, bum_cell_position) and the layer-B table below.
[[nodiscard]] CellPosition entity_layer_ab_position(int col, int row);

// Draw position of a layer-B grid cell (DS:0x3f4): x = 32 + col*40, y = row*32 --
// a distinct table from layer A (offset +32 x, -24 y). y_offset added by the caller.
[[nodiscard]] CellPosition entity_layer_b_position(int col, int row);

}  // namespace bumpy

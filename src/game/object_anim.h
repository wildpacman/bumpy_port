#pragma once

#include <cstddef>
#include <cstdint>

namespace bumpy {

// Tile bump/spring animation data, extracted from BUMPY.UNPACKED.EXE by
// tools/re/dump_object_anim.py (the definitions live in object_anim.gen.cpp).
//
// When the ball bumps a peg (layer A) or block (layer B), a per-step handler in
// the DS:0x43c0 dispatch starts an animation: the bumped grid cell's tile value
// is replaced with the event's `new_tile` (the settled look) and an animation
// slot plays the event's frame-byte stream, one sprite per frame. Each non-zero
// stream byte is a sprite index into the per-layer record table below, giving
// the bank frame + vertical anchor for that step. 0x00 holds the previous step's
// sprite; 0xff ends the animation. See analysis/specs/game-loop.md.

// A frame_index value meaning "draw nothing this step" (the original's 0x200
// flag bit, e.g. a blink-off frame). The sprite indices that map to it are kept
// in the streams so the step still consumes a frame of time.
inline constexpr std::uint16_t kAnimHiddenFrame = 0xffff;

// One animation step's sprite: a bank frame index and the Y offset added to the
// cell's draw position (taller frames sit lower, producing the squash bounce).
struct AnimRecord {
    std::uint16_t frame_index;
    std::uint8_t y_offset;
};

// A live bump animation as seen by the renderer: which grid cell to draw over,
// the current bank frame (kAnimHiddenFrame = draw nothing this step), the Y anchor
// offset, and whether it is a layer-B (block) vs layer-A (peg) sprite.
struct ObjectAnimSprite {
    std::uint8_t cell;
    std::uint16_t frame_index;
    std::uint8_t y_offset;
    bool layer_b;
};

// One bump event: the tile written into the grid plus a slice of the byte-stream
// pool (sprite indices; 0x00 = hold, 0xff = end). Indexed by event id; unused
// ids have stream_len == 0.
struct BumpEvent {
    std::uint8_t new_tile;
    std::uint16_t stream_offset;
    std::uint8_t stream_len;
};

// Sprite-index -> {frame, y_offset}. Layer A = pegs (DS:0x37be), layer B = blocks
// (DS:0x3ad2, frame already biased by +0xf1). Index 0 is unused (1-based).
extern const AnimRecord kAnimRecordA[];
extern const std::size_t kAnimRecordACount;
extern const AnimRecord kAnimRecordB[];
extern const std::size_t kAnimRecordBCount;

// Bump events + their pooled streams. Layer A (FUN_1000_69aa, DS:0x2ede), layer B
// (FUN_1000_6a89, DS:0x3256).
extern const BumpEvent kBumpEventA[];
extern const std::size_t kBumpEventACount;
extern const std::uint8_t kBumpEventAStream[];
extern const BumpEvent kBumpEventB[];
extern const std::size_t kBumpEventBCount;
extern const std::uint8_t kBumpEventBStream[];

// Layer-B neighbour bump: bumped cell's plane-B value -> event id, one 32-entry
// row per step handler (DS:0x35be..0x369e). Rows 0/2/4/6 bump the LEFT neighbour
// (6699/6748/67e2/68fe), rows 1/3/5/7 the RIGHT (66d8/6789/6813/693a).
extern const std::uint8_t kBumpSelectB[8][32];
enum BumpSelectRow {  // index into kBumpSelectB
    kBumpSelL0 = 0, kBumpSelR0 = 1,  // 6699 / 66d8  (hop up-left / up-right)
    kBumpSelL1 = 2, kBumpSelR1 = 3,  // 6748 / 6789
    kBumpSelL2 = 4, kBumpSelR2 = 5,  // 67e2 / 6813
    kBumpSelL3 = 6, kBumpSelR3 = 7,  // 68fe / 693a
};

// Layer-A spring maps: tile under the ball -> event id. kIdleSpringA is the
// rest/idle bob (FUN_1000_6987); kRollSpringL/R fire as a roll-left/right starts
// (the roll entries 6699/66d8 -> FUN_1000_6d6a) -- the platform recoiling as it
// deflects the ball sideways.
extern const std::uint8_t kIdleSpringA[0x30];
extern const std::uint8_t kRollSpringL[0x30];
extern const std::uint8_t kRollSpringR[0x30];

}  // namespace bumpy

#pragma once

#include "core/indexed_framebuffer.h"
#include "resources/level_resources.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bumpy {

struct BoardRenderStats {
    int objects_drawn{};   // single objects (cell byte 0 in 1..0xf0)
    int stacked_cells{};   // cells with byte 0 >= 0xf1 (each draws several stacked tiles)
    int stacked_tiles{};   // total tiles stamped by stacked cells
};

// Compose one static playfield board onto a 320x200 indexed frame.
//
// The original draws each board in FUN_1000_2a0a: for every cell it first runs the
// "base tile" (FUN_1000_0b88) then, when cell byte 0 != 0, the PAV object
// (FUN_1000_0a90). The base tile is NOT a floor sprite -- recovered from the
// overlay planar path 1ab9:0179 / 1ab9:002b, it is a fixed planar fill from command
// bytes +0x22..+0x25, which FUN_1000_0b88 zeroes, so it just clears each cell to
// colour index 0. We reproduce it as a full-frame index-0 clear (see
// analysis/specs/level-formats.md "Base-Tile Blit Recovery").
//
// Objects: index obj in 1..0xf0 -> sheet tile ((obj-1)%20, (obj-1)/20); obj >= 0xf1
// is a stacked marker that stamps (uint8_t)(-obj-5) tiles taken from the following
// cell bytes, all at the same cell. Colour index 0 is transparent.
//
// The backdrop screen supplies the per-world VGA palette (the working hypothesis
// for the gameplay palette). draw_map overlays the MONDE screen pixels instead of
// the faithful flat clear -- but that screen is the world-select map, so it is a
// debug aid only, off by default.
BoardRenderStats render_board(const LevelResources& level, std::size_t board_index,
                              std::span<const std::uint8_t> backdrop_screen,
                              IndexedFramebuffer& target, bool draw_map = false);

struct EntityOverlayStats {
    int layer_a{};  // peg/bumper cells
    int layer_b{};  // second-layer cells
    int layer_c{};  // collectible cells
};

// Overlay the decoded BUM entity grid onto an already-composed board, for by-eye
// inspection of the recovered layout. A marker is drawn per occupied cell at its
// faithful DS:0x274 screen position (see bum_cell_position): layer A (pegs) a
// small centred dot, layer B a filled square, layer C (collectibles) a hollow
// box. These are NOT the original entity sprites -- those live in an
// overlay/BUMSPJEU sprite bank that needs the compressed-sprite decoder -- so the
// markers validate only the decoded grid and positions, not the final art.
EntityOverlayStats overlay_bum_entities(const BumEntities& bum, IndexedFramebuffer& target,
                                        std::uint8_t color_a = 15, std::uint8_t color_b = 9,
                                        std::uint8_t color_c = 14);

}  // namespace bumpy

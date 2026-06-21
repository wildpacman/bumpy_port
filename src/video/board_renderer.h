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
// The gameplay palette is the board's OWN palette (LevelBoard::palette(), decoded
// from the DEC board header) -- the playfield does not inherit the brown MONDE map
// palette. backdrop_screen is used only by draw_map, which overlays the world-select
// MONDE screen (pixels + its palette) instead of the faithful flat clear, as a debug
// aid; it is off by default.
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
// box. These are inspection markers, not the original art -- use draw_bum_entities
// for the real sprites.
EntityOverlayStats overlay_bum_entities(const BumEntities& bum, IndexedFramebuffer& target,
                                        std::uint8_t color_a = 15, std::uint8_t color_b = 9,
                                        std::uint8_t color_c = 14);

// Draw the real BUM entity sprites onto an already-composed board, reading frames
// directly from the uncompressed BUMSPJEU.BIN bank. Each occupied cell resolves to
// a bank frame index (entity_sprites.h) which is decoded via decode_sprite_frame
// and blitted at its faithful screen position: layers A/B use DS:0xf4 (x=col*40,
// y=24+row*32 + the sprite's count offset), layer C uses DS:0x274
// (bum_cell_position). Colour index 0 is transparent. Mirrors the spawn loop
// FUN_1000_2a78 (per cell: layer A, then B, then C; layer B never draws col 7).
// Frames that fail to decode are skipped (counted in skipped). The sprite_bank
// span is the whole BUMSPJEU.BIN.
struct EntitySpriteStats {
    int layer_a{};
    int layer_b{};
    int layer_c{};
    int skipped{};  // cells whose frame index did not decode
};
EntitySpriteStats draw_bum_entities(const BumEntities& bum,
                                    std::span<const std::uint8_t> sprite_bank,
                                    IndexedFramebuffer& target);

}  // namespace bumpy

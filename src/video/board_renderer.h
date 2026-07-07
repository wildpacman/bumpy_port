#pragma once

#include "core/indexed_framebuffer.h"
#include "game/object_anim.h"  // ObjectAnimSprite
#include "resources/level_resources.h"

#include <cstddef>
#include <cstdint>
#include <functional>
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

enum class EntityLayer { a, b, c };

// Enumerate the BUM grid's entity sprites in the faithful draw order of
// FUN_1000_2a78 (row-major; per cell layer A, then B -- never col 7 --, then C),
// yielding each sprite's bank frame index and exact blit top-left. The single
// source of entity placement, shared by draw_bum_entities (flat path) and the
// 3D scene builder, so both compose identically by construction.
void for_each_entity_sprite(
    const BumEntities& bum,
    const std::function<void(EntityLayer layer, int frame_index, int x, int y)>& fn);

EntitySpriteStats draw_bum_entities(const BumEntities& bum,
                                    std::span<const std::uint8_t> sprite_bank,
                                    IndexedFramebuffer& target);

// Draw the player ball: BUMSPJEU bank frame `frame` (the move-script frame DAT_824a)
// at its logical position (ball_x, ball_y) = DS:0x9290/0x9292, which is the cell slot
// plus the (+7,+15) ball offset (FUN_1000_4906). The sprite is drawn bottom-centred on
// that anchor so it sits on the cell like the original. The hidden sentinel frame 100
// (FUN_1000_1cb2 skips it) and any frame that fails to decode draw nothing. Returns
// true if the ball was drawn.
bool draw_ball(std::span<const std::uint8_t> sprite_bank, int frame, int ball_x, int ball_y,
               IndexedFramebuffer& target);

// Draw the moving entity (monster): BUMSPJEU bank frame `frame` (= a0de + the move
// keyframe) centred on its pixel anchor (mon_x, mon_y) = DS:0x79ba/0x79bc, the cell
// slot + (7,7) (FUN_1000_48a9). Same centre-on-anchor blit as the ball; colour index
// 0 is transparent. Returns true if drawn. Call only when monster_present(). FUN_1000_1cea.
bool draw_monster(std::span<const std::uint8_t> sprite_bank, int frame, int mon_x, int mon_y,
                  IndexedFramebuffer& target);

// Overlay the live tile bump/spring animations (LevelGame::object_anims) on top of
// the static board. Each entry draws its bank frame at the cell's layer-A/B screen
// slot (entity_layer_ab_position) plus the step's y_offset, exactly like the static
// peg/block sprites; a kAnimHiddenFrame step (blink-off) and any frame that fails to
// decode draw nothing. The animating cell's static tile should be suppressed by the
// caller so only the moving sprite shows (mirrors the original's background restore).
// Returns the number of frames actually drawn.
int draw_object_anims(std::span<const ObjectAnimSprite> anims,
                      std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);

}  // namespace bumpy

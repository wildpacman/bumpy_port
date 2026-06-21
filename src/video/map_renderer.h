#pragma once

#include "core/indexed_framebuffer.h"
#include "game/world_map.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bumpy {

struct MapRenderStats {
    bool avatar_drawn{};
};

// The Bumpy avatar's sprite frame. The original map avatar (FUN_1000_1cb2 blits
// frame DAT_824a, set to 0x21 by FUN_1000_3852) reads the *player* sprite bank --
// BUMPYSPR.BIN / SPRITE.BIN -- which are NOT shipped with these assets. The supplied
// BUMSPJEU.BIN (gameplay bank) carries equivalent Bumpy player faces at frames
// 0x00..0x0c; 0x08 is the forward-facing idle that matches the world-1 capture
// (screenshots/bumpy_001.png) by eye. So this is a faithful substitute, not the
// original index. (Reading 0x21 from BUMSPJEU drew unrelated garbage.)
inline constexpr int map_avatar_frame = 0x08;

// Compose the world-map screen: deplane the MONDE backdrop with its embedded palette,
// then blit the Bumpy avatar (see map_avatar_frame) at the current node's avatar
// position (colour index 0 transparent). monde_screen is a decoded 320x200
// screen-format VEC; sprite_bank is the whole BUMSPJEU.BIN. If the avatar frame fails
// to decode it is skipped (avatar_drawn stays false) rather than throwing.
MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view,
                          std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);

}  // namespace bumpy

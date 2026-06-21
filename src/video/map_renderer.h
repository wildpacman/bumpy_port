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

// The Bumpy avatar's sprite frame: BUMSPJEU.BIN frame 0x21, the Bumpy-on-cloud idle
// (FUN_1000_1cb2 blits frame DAT_824a, which FUN_1000_3852 sets to 0x21). This is the
// real avatar -- it only looked like garbage before because the sprite decoder used
// the wrong plane layout for 32px-wide frames (see sprite_frame.cpp's group note).
// Frames 0x22.. are the cloud-squash/jump animation; 0x21 is the resting pose that
// matches the world-1 capture (screenshots/bumpy_001.png).
inline constexpr int map_avatar_frame = 0x21;

// Compose the world-map screen: deplane the MONDE backdrop with its embedded palette,
// then blit the Bumpy avatar (see map_avatar_frame) at the current node's avatar
// position (colour index 0 transparent). monde_screen is a decoded 320x200
// screen-format VEC; sprite_bank is the whole BUMSPJEU.BIN. If the avatar frame fails
// to decode it is skipped (avatar_drawn stays false) rather than throwing.
MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view,
                          std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);

}  // namespace bumpy

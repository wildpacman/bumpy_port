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

// The Bumpy avatar's sprite frame in the BUMSPJEU.BIN bank, recovered from the map
// avatar draw FUN_1000_1cb2 (blits frame DAT_824a, which FUN_1000_3852 sets to 0x21).
inline constexpr int map_avatar_frame = 0x21;

// Compose the world-map screen: deplane the MONDE backdrop with its embedded palette,
// then blit the avatar (BUMSPJEU frame 0x21) at the current node's avatar position
// (colour index 0 transparent). monde_screen is a decoded 320x200 screen-format VEC;
// sprite_bank is the whole BUMSPJEU.BIN. If the avatar frame fails to decode it is
// skipped (avatar_drawn stays false) rather than throwing.
MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view,
                          std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);

}  // namespace bumpy

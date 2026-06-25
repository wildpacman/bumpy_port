#pragma once

#include "core/indexed_framebuffer.h"
#include "game/world_map.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bumpy {

struct MapRenderStats {
    bool avatar_drawn{};
    int markers_drawn{};  // completed-node markers blitted (frame 0x1da)
};

// Compose the world-map screen: deplane the MONDE backdrop with its embedded palette,
// then blit the Bumpy avatar -- view.avatar_frame (kRestingAvatarFrame 0x21 at rest, the
// jump script's ball frames while jumping, kAvatarFrameHidden to vanish) plus the launch
// cloud (kJumpCloudFrame) when view.cloud_visible. Frames are centred horizontally on the
// node within the resting frame's 32x21 box (the ball top-aligned and bounced by
// view.avatar_offset_y, the cloud bottom-aligned), so the resting pose matches
// screenshots/bumpy_001.png and the jump begins exactly where it sits. monde_screen is a
// decoded 320x200 screen-format VEC; sprite_bank is the whole BUMSPJEU.BIN. Frames that
// fail to decode are skipped rather than throwing.
//
// cleared_boards (optional) is the per-board completion flags (0/1) indexed by
// board = node - 1: every set entry draws the completed-node marker (frame 0x1da)
// on its node, beneath the avatar. Empty -> no markers (the dev dumpers pass none).
MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view,
                          std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target,
                          std::span<const std::uint8_t> cleared_boards = {});

}  // namespace bumpy

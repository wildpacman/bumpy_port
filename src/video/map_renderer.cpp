#include "video/map_renderer.h"

#include "resources/sprite_frame.h"
#include "video/menu_renderer.h"  // MenuImage
#include "video/screen_image.h"

#include <exception>

namespace bumpy {
namespace {

// The resting avatar (frame 0x21) is a 32x21 composite -- Bumpy (the ball) on top of a
// cloud -- drawn bbox-centred on the node ring (verified against
// screenshots/bumpy_001.png). The jump animation splits that composite into its parts:
// the ball plays as the smaller frames 0..7 / 0x13..0x1f (frame 0 is pixel-identical to
// the top of 0x21, centred at content offset (8,0)) and the cloud is frame 0xcb
// (pixel-identical to the bottom of 0x21, at content offset (0,10)). Placing every frame
// horizontally centred on the node within this same 32x21 box -- the ball top-aligned
// (then bounced by avatar_offset_y), the cloud bottom-aligned -- makes the jump start
// exactly where the resting pose is and keeps the cloud stationary while Bumpy bounces.
constexpr int avatar_box_w = 32;  // frame 0x21 width
constexpr int avatar_box_h = 21;  // frame 0x21 height

// Blit a decoded sprite frame at the given top-left, clipped to the target, colour
// index 0 (sprite_transparent_index) skipped.
void blit_sprite(const MenuImage& image, int top_x, int top_y, IndexedFramebuffer& target) {
    for (int py = 0; py < image.height; ++py) {
        const int ty = top_y + py;
        if (ty < 0 || ty >= target.height()) {
            continue;
        }
        for (int px = 0; px < image.width; ++px) {
            const int tx = top_x + px;
            if (tx < 0 || tx >= target.width()) {
                continue;
            }
            const auto color = image.pixels[static_cast<std::size_t>(py) * image.width + px];
            if (color != sprite_transparent_index) {
                target.pixel(tx, ty) = color;
            }
        }
    }
}

}  // namespace

MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view,
                          std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target) {
    apply_screen_image_palette(monde_screen, target);
    draw_screen_image(monde_screen, target);

    // The avatar's 32x21 bounding box, bbox-centred on the node ring.
    const int box_left = view.avatar_x - avatar_box_w / 2;
    const int box_top = view.avatar_y - avatar_box_h / 2;

    // The launch cloud sits at the bottom of that box (where the resting pose's cloud
    // is), centred horizontally; it stays put while the ball bounces above it.
    if (view.cloud_visible) {
        try {
            const MenuImage cloud = decode_sprite_frame(sprite_bank, kJumpCloudFrame);
            blit_sprite(cloud, view.avatar_x - cloud.width / 2,
                        box_top + (avatar_box_h - cloud.height), target);
        } catch (const std::exception&) {
            // cloud frame unavailable -> skip it
        }
    }

    MapRenderStats stats;
    // The avatar frame: centred horizontally on the node, top-aligned in the box, then
    // bounced vertically by avatar_offset_y (the jump is purely vertical, dx = 0).
    if (view.avatar_frame != kAvatarFrameHidden) {
        try {
            const MenuImage avatar = decode_sprite_frame(sprite_bank, view.avatar_frame);
            blit_sprite(avatar, view.avatar_x - avatar.width / 2, box_top + view.avatar_offset_y,
                        target);
            stats.avatar_drawn = true;
        } catch (const std::exception&) {
            // avatar frame unavailable -> map renders without it
        }
    }
    return stats;
}

}  // namespace bumpy

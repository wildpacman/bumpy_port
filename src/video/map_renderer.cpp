#include "video/map_renderer.h"

#include "resources/sprite_frame.h"
#include "video/menu_renderer.h"  // MenuImage
#include "video/screen_image.h"

#include <exception>

namespace bumpy {

MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view,
                          std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target) {
    apply_screen_image_palette(monde_screen, target);
    draw_screen_image(monde_screen, target);

    MapRenderStats stats;
    MenuImage avatar;
    try {
        avatar = decode_sprite_frame(sprite_bank, map_avatar_frame);
    } catch (const std::exception&) {
        return stats;  // avatar frame unavailable -> map renders without it
    }

    // The node position is the ring centre, and the avatar frame fills its 32x21
    // bounds (no transparent margin), so centre the frame on the node -- verified by
    // eye against screenshots/bumpy_001.png, where Bumpy sits centred in node 1's ring.
    const int top_x = view.avatar_x - avatar.width / 2;
    const int top_y = view.avatar_y - avatar.height / 2;
    for (int py = 0; py < avatar.height; ++py) {
        const int ty = top_y + py;
        if (ty < 0 || ty >= target.height()) {
            continue;
        }
        for (int px = 0; px < avatar.width; ++px) {
            const int tx = top_x + px;
            if (tx < 0 || tx >= target.width()) {
                continue;
            }
            const auto color = avatar.pixels[static_cast<std::size_t>(py) * avatar.width + px];
            if (color != sprite_transparent_index) {
                target.pixel(tx, ty) = color;
            }
        }
    }
    stats.avatar_drawn = true;
    return stats;
}

}  // namespace bumpy

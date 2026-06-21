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

    for (int py = 0; py < avatar.height; ++py) {
        const int ty = view.avatar_y + py;
        if (ty < 0 || ty >= target.height()) {
            continue;
        }
        for (int px = 0; px < avatar.width; ++px) {
            const int tx = view.avatar_x + px;
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

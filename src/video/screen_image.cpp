#include "video/screen_image.h"

#include "video/menu_renderer.h"  // vga_dac_to_rgba_component

namespace bumpy {

bool is_screen_image(std::span<const std::uint8_t> screen) noexcept {
    return screen.size() >= screen_image_pixel_offset + 4 * screen_image_plane;
}

void apply_screen_image_palette(std::span<const std::uint8_t> screen, IndexedFramebuffer& target) {
    const std::uint8_t* palette = screen.data() + screen_image_palette_offset;
    for (int color = 0; color < screen_image_palette_colors; ++color) {
        const std::uint8_t* entry = palette + color * 3;
        target.set_palette(static_cast<std::uint8_t>(color),
                           Rgba{vga_dac_to_rgba_component(entry[0]),
                                vga_dac_to_rgba_component(entry[1]),
                                vga_dac_to_rgba_component(entry[2]), 0xff});
    }
}

void draw_screen_image(std::span<const std::uint8_t> screen, IndexedFramebuffer& target) {
    const std::uint8_t* planes = screen.data() + screen_image_pixel_offset;
    for (std::size_t pixel = 0;
         pixel < static_cast<std::size_t>(screen_image_width) * screen_image_height; ++pixel) {
        const std::size_t byte = pixel >> 3U;
        const unsigned shift = 7U - static_cast<unsigned>(pixel & 7U);
        std::uint8_t value = 0;
        for (int plane = 0; plane < 4; ++plane) {
            value = static_cast<std::uint8_t>(
                value | (((planes[plane * screen_image_plane + byte] >> shift) & 1U) << plane));
        }
        target.pixel(static_cast<int>(pixel % screen_image_width),
                     static_cast<int>(pixel / screen_image_width)) = value;
    }
}

}  // namespace bumpy

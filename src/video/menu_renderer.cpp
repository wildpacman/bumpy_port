#include "video/menu_renderer.h"

#include "resources/sprite_frame.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

// The decoded menu screens (TITRE.VEC and the per-world .VEC files) are full
// 320x200 frames stored as a 99-byte header followed by four 8000-byte VGA
// bit-planes (plane-sequential, most-significant bit first). The header carries
// the 16-colour VGA DAC palette as 16 RGB triplets ending right before the pixel
// data. Recovered by deplaning TITRE.VEC and comparing the result by eye to the
// original title screen; see analysis/specs/menu-resource-formats.md.
constexpr int screen_width = 320;
constexpr int screen_height = 200;
constexpr int plane_count = 4;
constexpr std::size_t plane_size =
    static_cast<std::size_t>(screen_width) * static_cast<std::size_t>(screen_height) / 8;  // 8000
constexpr std::size_t pixel_data_offset = 99;
constexpr int palette_color_count = 16;
constexpr std::size_t palette_offset = pixel_data_offset - palette_color_count * 3;  // 51
constexpr int marker_x = 48;
constexpr int marker_y = 112;
constexpr int marker_row_height = 16;

bumpy::MenuImage deplane_screen(std::span<const std::uint8_t> decoded) {
    const std::size_t required = pixel_data_offset + plane_count * plane_size;
    if (decoded.size() < required) {
        throw std::runtime_error("decoded menu screen is shorter than four 320x200 bit-planes");
    }
    const std::uint8_t* planes = decoded.data() + pixel_data_offset;
    bumpy::MenuImage image{
        screen_width,
        screen_height,
        std::vector<std::uint8_t>(static_cast<std::size_t>(screen_width) * screen_height),
    };
    for (std::size_t pixel = 0; pixel < image.pixels.size(); ++pixel) {
        const std::size_t byte = pixel >> 3U;
        const unsigned shift = 7U - static_cast<unsigned>(pixel & 7U);
        std::uint8_t value = 0;
        for (int plane = 0; plane < plane_count; ++plane) {
            const std::uint8_t bit = (planes[plane * plane_size + byte] >> shift) & 1U;
            value = static_cast<std::uint8_t>(value | (bit << plane));
        }
        image.pixels[pixel] = value;
    }
    return image;
}

void apply_screen_palette(std::span<const std::uint8_t> decoded, bumpy::IndexedFramebuffer& target) {
    if (decoded.size() < palette_offset + palette_color_count * 3) {
        throw std::runtime_error("decoded menu screen is too short to hold the 16-colour palette");
    }
    const std::uint8_t* palette = decoded.data() + palette_offset;
    for (int color = 0; color < palette_color_count; ++color) {
        const std::uint8_t* entry = palette + color * 3;
        target.set_palette(
            static_cast<std::uint8_t>(color),
            bumpy::Rgba{
                bumpy::vga_dac_to_rgba_component(entry[0]),
                bumpy::vga_dac_to_rgba_component(entry[1]),
                bumpy::vga_dac_to_rgba_component(entry[2]),
                0xff,
            });
    }
}

void validate_image(const bumpy::MenuImage& image) {
    if (image.width < 0 || image.height < 0) {
        throw std::runtime_error("menu image has negative dimensions");
    }
    const auto expected = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    if (image.pixels.size() != expected) {
        throw std::runtime_error("menu image pixel count does not match dimensions");
    }
}

void validate_command(const bumpy::MenuDrawCommand& command) {
    validate_image(command.image);
    if (command.width < 0 || command.height < 0) {
        throw std::runtime_error("menu draw command has negative dimensions");
    }
    if (command.source_x < 0 || command.source_y < 0 ||
        command.source_x + command.width > command.image.width ||
        command.source_y + command.height > command.image.height) {
        throw std::runtime_error("menu draw command source rectangle is outside image");
    }
    if (!command.mask.empty()) {
        const auto expected = static_cast<std::size_t>(command.width) * static_cast<std::size_t>(command.height);
        if (command.mask.size() != expected) {
            throw std::runtime_error("menu draw command mask size does not match rectangle");
        }
    }
}

}  // namespace

namespace bumpy {

std::uint8_t vga_dac_to_rgba_component(std::uint8_t value) noexcept {
    const auto clamped = static_cast<std::uint8_t>(std::min<std::uint8_t>(value, 63));
    return static_cast<std::uint8_t>((clamped << 2U) | (clamped >> 4U));
}

void draw_menu_command(const MenuDrawCommand& command, IndexedFramebuffer& target) {
    validate_command(command);

    for (int source_y = 0; source_y < command.height; ++source_y) {
        const auto target_y = command.destination_y + source_y;
        if (target_y < 0 || target_y >= target.height()) {
            continue;
        }
        for (int source_x = 0; source_x < command.width; ++source_x) {
            const auto target_x = command.destination_x + source_x;
            if (target_x < 0 || target_x >= target.width()) {
                continue;
            }

            const auto command_offset = static_cast<std::size_t>(source_y * command.width + source_x);
            if (!command.mask.empty() && command.mask[command_offset] == 0) {
                continue;
            }

            const auto image_offset = static_cast<std::size_t>(
                (command.source_y + source_y) * command.image.width + (command.source_x + source_x));
            const auto color = command.image.pixels[image_offset];
            if (command.transparent_index && color == *command.transparent_index) {
                continue;
            }
            target.pixel(target_x, target_y) = color;
        }
    }
}

MenuRenderer::MenuRenderer(const MenuResources& resources) : resources_(resources) {}

void MenuRenderer::render(const MenuView& view, IndexedFramebuffer& target) const {
    target.clear(0);
    const auto title = resources_.title.decoded_bytes();

    // The screen carries its own VGA palette; install it before compositing so the
    // indexed frame resolves to the original colours.
    apply_screen_palette(title, target);

    if (view.draw_title) {
        draw_menu_command(
            MenuDrawCommand{deplane_screen(title), 0, 0, screen_width, screen_height, 0, 0},
            target);
    }

    if (view.draw_cursor_marker) {
        const auto marker = decode_sprite_frame(resources_.cursor_sprites, 0);
        draw_menu_command(
            MenuDrawCommand{
                marker,
                0,
                0,
                marker.width,
                marker.height,
                marker_x,
                marker_y + view.cursor_row * marker_row_height,
                sprite_transparent_index,
            },
            target);
    }
}

}  // namespace bumpy

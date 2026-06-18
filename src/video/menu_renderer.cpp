#include "video/menu_renderer.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

constexpr int confirmed_title_width = 320;
constexpr int confirmed_title_height = 100;
constexpr int confirmed_title_pixel_count = confirmed_title_width * confirmed_title_height;
constexpr int cursor_source_x = 11;
constexpr int cursor_source_y = 18;
constexpr int cursor_width = 6;
constexpr int cursor_height = 2;
constexpr int cursor_destination_x = 0x30;
constexpr int cursor_destination_y = 0x70;
constexpr int cursor_row_stride = 0x10;

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
    if (title.size() < confirmed_title_pixel_count) {
        throw std::runtime_error("decoded title resource is shorter than confirmed title pixel plane");
    }

    if (view.draw_title) {
        MenuImage title_image{
            confirmed_title_width,
            confirmed_title_height,
            std::vector<std::uint8_t>(title.begin(), title.begin() + confirmed_title_pixel_count),
        };
        draw_menu_command(
            MenuDrawCommand{
                std::move(title_image),
                0,
                0,
                confirmed_title_width,
                confirmed_title_height,
                0,
                0,
            },
            target);
    }

    if (view.draw_cursor_marker) {
        MenuImage cursor_source{
            confirmed_title_width,
            confirmed_title_height,
            std::vector<std::uint8_t>(title.begin(), title.begin() + confirmed_title_pixel_count),
        };
        MenuDrawCommand cursor_command{
            std::move(cursor_source),
            cursor_source_x,
            cursor_source_y,
            cursor_width,
            cursor_height,
            cursor_destination_x,
            cursor_destination_y + view.cursor_row * cursor_row_stride,
        };
        cursor_command.transparent_index = 0;
        draw_menu_command(cursor_command, target);
    }
}

}  // namespace bumpy

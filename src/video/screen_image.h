#pragma once

#include "core/indexed_framebuffer.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bumpy {

// Screen-format VEC geometry (TITRE/MONDE?): a 99-byte header carrying the 16-colour
// VGA DAC palette (16 RGB triplets ending at the pixel data, so at offset 51), then
// four 8000-byte plane-sequential bit-planes for a 320x200 image.
inline constexpr int screen_image_width = 320;
inline constexpr int screen_image_height = 200;
inline constexpr std::size_t screen_image_plane =
    static_cast<std::size_t>(screen_image_width) * screen_image_height / 8;  // 8000
inline constexpr std::size_t screen_image_pixel_offset = 99;
inline constexpr std::size_t screen_image_palette_offset = screen_image_pixel_offset - 16 * 3;  // 51

// True when `screen` is large enough to be a 320x200 screen-format VEC.
[[nodiscard]] bool is_screen_image(std::span<const std::uint8_t> screen) noexcept;

// Set framebuffer palette entries 0..15 from the screen's embedded DAC palette.
void apply_screen_image_palette(std::span<const std::uint8_t> screen, IndexedFramebuffer& target);

// Deplane the four bit-planes into the framebuffer (full 320x200 overwrite).
void draw_screen_image(std::span<const std::uint8_t> screen, IndexedFramebuffer& target);

}  // namespace bumpy

#pragma once

#include "video/menu_renderer.h"

#include <cstdint>
#include <span>

namespace bumpy {

inline constexpr std::uint8_t sprite_transparent_index = 0xff;

MenuImage decode_sprite_frame(std::span<const std::uint8_t> archive_data, int frame_index);

}  // namespace bumpy

#include "resources/sfx_tables.h"

#include <cstddef>

namespace bumpy {
namespace {

template <std::size_t N>
std::uint8_t lookup(const std::uint8_t (&table)[N], std::uint8_t index) noexcept {
    return index < N ? table[index] : 0;
}

}  // namespace

std::uint8_t sfx_idle_rest(std::uint8_t tile) noexcept {
    return lookup(kSfxIdleRest, tile);
}

std::uint8_t sfx_roll_bump(std::uint8_t tile) noexcept {
    return lookup(kSfxRollBump, tile);
}

std::uint8_t sfx_fall_route(std::uint8_t tile) noexcept {
    return lookup(kSfxFallRoute, tile);
}

std::uint8_t sfx_held_bump(std::uint8_t tile) noexcept {
    return lookup(kSfxHeldBump, tile);
}

std::uint8_t sfx_layer_b_block(std::uint8_t event_id) noexcept {
    return lookup(kSfxLayerBBlock, event_id);
}

std::uint8_t sfx_picture_block(std::uint8_t plane_b_value) noexcept {
    return lookup(kSfxPictureBlock, plane_b_value);
}

}  // namespace bumpy


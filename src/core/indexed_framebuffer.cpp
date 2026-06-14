#include "core/indexed_framebuffer.h"

#include <stdexcept>

namespace bumpy {

IndexedFramebuffer::IndexedFramebuffer(int width, int height)
    : width_(width), height_(height) {
    if (width_ <= 0 || height_ <= 0) {
        throw std::invalid_argument("framebuffer dimensions must be positive");
    }
    pixels_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
}

std::uint8_t& IndexedFramebuffer::pixel(int x, int y) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        throw std::out_of_range("framebuffer coordinates are outside the image");
    }
    return pixels_.at(static_cast<std::size_t>(y * width_ + x));
}

void IndexedFramebuffer::set_palette(std::uint8_t index, Rgba color) {
    palette_[index] = color;
}

std::vector<std::uint32_t> IndexedFramebuffer::to_rgba() const {
    std::vector<std::uint32_t> result;
    result.reserve(pixels_.size());
    for (const auto index : pixels_) {
        const auto color = palette_[index];
        result.push_back(
            static_cast<std::uint32_t>(color.r) |
            (static_cast<std::uint32_t>(color.g) << 8) |
            (static_cast<std::uint32_t>(color.b) << 16) |
            (static_cast<std::uint32_t>(color.a) << 24));
    }
    return result;
}

}  // namespace bumpy

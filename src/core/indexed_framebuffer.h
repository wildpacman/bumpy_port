#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace bumpy {

struct Rgba {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

class IndexedFramebuffer {
public:
    IndexedFramebuffer(int width, int height);
    std::uint8_t& pixel(int x, int y);
    void set_palette(std::uint8_t index, Rgba color);
    [[nodiscard]] std::vector<std::uint32_t> to_rgba() const;
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    int width_;
    int height_;
    std::vector<std::uint8_t> pixels_;
    std::array<Rgba, 256> palette_{};
};

}  // namespace bumpy

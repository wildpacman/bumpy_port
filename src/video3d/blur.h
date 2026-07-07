#pragma once

#include <cstdint>
#include <vector>

namespace bumpy {

// Separable gaussian blur, clamped edges, radius = ceil(3*sigma). sigma <= 0: no-op.
// rgba: w*h*4 bytes, rows top-to-bottom (each channel blurred independently).
void gaussian_blur_rgba(std::vector<std::uint8_t>& rgba, int w, int h, float sigma);
// Single-channel variant (w*h bytes) -- used for baked sprite shadow silhouettes.
void gaussian_blur_alpha(std::vector<std::uint8_t>& alpha, int w, int h, float sigma);

}  // namespace bumpy

#include "video3d/blur.h"

#include <algorithm>
#include <cmath>

namespace bumpy {

namespace {

std::vector<float> gaussian_kernel(float sigma) {
    const int radius = std::min(static_cast<int>(std::ceil(3.0f * sigma)), 256);
    std::vector<float> k(static_cast<std::size_t>(2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float v = std::exp(-(static_cast<float>(i) * i) / (2.0f * sigma * sigma));
        k[static_cast<std::size_t>(i + radius)] = v;
        sum += v;
    }
    for (auto& v : k) {
        v /= sum;
    }
    return k;
}

// One separable pass over `src` (stride `channels`, channel `c`), writing `dst`.
void blur_axis(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& dst, int w,
               int h, int channels, int c, const std::vector<float>& kernel, bool horizontal) {
    const int radius = static_cast<int>(kernel.size() / 2);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                const int sx = horizontal ? std::clamp(x + i, 0, w - 1) : x;
                const int sy = horizontal ? y : std::clamp(y + i, 0, h - 1);
                acc += kernel[static_cast<std::size_t>(i + radius)] *
                       src[(static_cast<std::size_t>(sy) * w + sx) * channels + c];
            }
            dst[(static_cast<std::size_t>(y) * w + x) * channels + c] =
                static_cast<std::uint8_t>(std::lround(std::clamp(acc, 0.0f, 255.0f)));
        }
    }
}

void blur(std::vector<std::uint8_t>& pixels, int w, int h, int channels, float sigma) {
    if (!(sigma > 0.0f) || w <= 0 || h <= 0) {  // !(>0) also catches NaN -> no-op
        return;
    }
    const auto kernel = gaussian_kernel(sigma);
    std::vector<std::uint8_t> tmp(pixels.size());
    for (int c = 0; c < channels; ++c) {
        blur_axis(pixels, tmp, w, h, channels, c, kernel, /*horizontal=*/true);
    }
    for (int c = 0; c < channels; ++c) {
        blur_axis(tmp, pixels, w, h, channels, c, kernel, /*horizontal=*/false);
    }
}

}  // namespace

void gaussian_blur_rgba(std::vector<std::uint8_t>& rgba, int w, int h, float sigma) {
    blur(rgba, w, h, 4, sigma);
}

void gaussian_blur_alpha(std::vector<std::uint8_t>& alpha, int w, int h, float sigma) {
    blur(alpha, w, h, 1, sigma);
}

}  // namespace bumpy

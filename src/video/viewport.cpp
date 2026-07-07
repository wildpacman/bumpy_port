#include "video/viewport.h"

#include <algorithm>
#include <cmath>

namespace bumpy {

Viewport compute_letterbox_viewport(int window_w, int window_h,
                                    int logical_w, int logical_h) noexcept {
    if (window_w <= 0 || window_h <= 0 || logical_w <= 0 || logical_h <= 0) {
        return {};
    }
    const double scale = std::min(static_cast<double>(window_w) / logical_w,
                                  static_cast<double>(window_h) / logical_h);
    const int w = std::max(1, static_cast<int>(std::lround(logical_w * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(logical_h * scale)));
    return {(window_w - w) / 2, (window_h - h) / 2, w, h};
}

}  // namespace bumpy

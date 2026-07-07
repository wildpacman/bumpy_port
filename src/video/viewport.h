#pragma once

namespace bumpy {

struct Viewport {
    int x{};
    int y{};
    int w{};
    int h{};
};

// The largest logical_w:logical_h rectangle that fits (window_w, window_h),
// centred -- the letterbox rect SDL_LOGICAL_PRESENTATION_LETTERBOX would pick.
// Degenerate inputs (any dimension <= 0) return an empty viewport.
[[nodiscard]] Viewport compute_letterbox_viewport(int window_w, int window_h,
                                                  int logical_w, int logical_h) noexcept;

}  // namespace bumpy

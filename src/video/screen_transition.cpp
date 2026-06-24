#include "video/screen_transition.h"

namespace bumpy {

void ScreenTransition::begin(const IndexedFramebuffer& outgoing) {
    width_ = outgoing.width();
    height_ = outgoing.height();
    const auto px = outgoing.pixels();
    snapshot_.assign(px.begin(), px.end());
    palette_ = outgoing.palette();
    step_ = 1;  // draw the outermost ring on the first frame, like the original's ring 0
    active_ = true;
}

void ScreenTransition::advance() noexcept {
    if (!active_) {
        return;
    }
    ++step_;
    if (step_ > kSteps) {
        active_ = false;  // last step rendered fully black; the new screen follows
    }
}

void ScreenTransition::render(IndexedFramebuffer& out) const {
    for (int i = 0; i < 256; ++i) {
        out.set_palette(static_cast<std::uint8_t>(i), palette_[static_cast<std::size_t>(i)]);
    }
    const int bx = kCellW * step_;  // black border thickness in px (left/right)
    const int by = kCellH * step_;  // black border thickness in px (top/bottom)
    for (int y = 0; y < height_; ++y) {
        const bool y_border = y < by || y >= height_ - by;
        for (int x = 0; x < width_; ++x) {
            const bool border = y_border || x < bx || x >= width_ - bx;
            out.pixel(x, y) = border
                ? std::uint8_t{0}
                : snapshot_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                            static_cast<std::size_t>(x)];
        }
    }
}

}  // namespace bumpy

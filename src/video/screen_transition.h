#pragma once

#include "core/indexed_framebuffer.h"

#include <array>
#include <cstdint>
#include <vector>

namespace bumpy {

// The original darkens the screen from the edges inward on every screen change
// (FUN_1000_3467, called at the start of the menu/world-map/password screens and before a
// board loads). It paints concentric black rings over the *outgoing* screen, outermost
// first, in 20x25 character cells of 16x8 px: each ring is a top/bottom/left/right black
// bar, the bars shrinking two cells per ring, and the 10 rings together cover the whole
// screen. The fill colour is index 0 (the screens' black background).
//
// This reproduces it as a growing black border over a snapshot of the outgoing frame.
// After step s the still-visible centre is the pixel rectangle
// [kCellW*s, W-kCellW*s) x [kCellH*s, H-kCellH*s); at s == kSteps it is empty, so the
// frame is fully black. The original runs the fill as one un-paced burst; the port owns
// the pacing in the run loop (it holds each ring for a few retraces) so the close reads
// as the same edges->centre wipe. This class is pure geometry: one ring per advance().
//
// Usage (in the platform run loop): on a screen change, begin() with the last-rendered
// outgoing frame, then each tick render() the current step into the framebuffer, present,
// and advance(); the new screen renders normally once active() goes false.
class ScreenTransition {
public:
    static constexpr int kCellW = 16;   // character-cell width  (320 / 20)
    static constexpr int kCellH = 8;    // character-cell height (200 / 25)
    static constexpr int kSteps = 10;   // 20 cells wide / 2 -> 10 rings reach the centre

    // Snapshot the outgoing screen (indices + palette) and arm the wipe at its first
    // (outermost) step, matching the original drawing ring 0 immediately.
    void begin(const IndexedFramebuffer& outgoing);

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] int step() const noexcept { return step_; }

    // Advance one ring toward the centre; deactivates after the last (fully black) step.
    void advance() noexcept;

    // Draw the outgoing snapshot with the current step's black border into `out`
    // (same dimensions as the snapshotted frame). Restores the outgoing palette so the
    // index-0 border and the still-visible centre render in the outgoing screen's colours.
    void render(IndexedFramebuffer& out) const;

private:
    bool active_{false};
    int step_{0};
    int width_{0};
    int height_{0};
    std::vector<std::uint8_t> snapshot_;
    std::array<Rgba, 256> palette_{};
};

}  // namespace bumpy

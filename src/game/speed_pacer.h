#pragma once

#include <cstdint>

namespace bumpy {

// The LEVEL menu item (FUN_1000_35a5 row 2) is a difficulty / game-speed selector.
// On menu exit the selection 0/1/2 indexes the 3-byte table at DS:0x11b2 = {0xff,
// 0xaa, 0x00} into DAT_203b_854f. In the in-level frame loop (FUN_1000_0c18)
// FUN_1000_1349 runs once per frame: it waits (854f low bit ? 2 : 1) vertical
// retraces then rotates 854f right. More set bits => more waits => slower => easier.
//   0 EASY   = 0xff  -> 2 retraces every frame  = 35.043 Hz (the port's historical pace)
//   1 MEDIUM = 0xaa  -> alternate 2/1 retraces  = ~46.7 Hz
//   2 HARD   = 0x00  -> 1 retrace every frame   = 70.086 Hz (2x faster)
// See analysis/specs/menu-behavior.md ("Difficulty selection").
[[nodiscard]] constexpr std::uint8_t level_speed_pattern(std::uint8_t difficulty) noexcept {
    constexpr std::uint8_t patterns[3] = {0xff, 0xaa, 0x00};
    return patterns[difficulty < 3U ? difficulty : 0U];
}

// The per-frame in-level pacer, a 1:1 transcription of FUN_1000_1349 (minus the
// FUN_1000_05e7/9864 retrace waits, which the platform shell performs instead).
class SpeedPacer {
public:
    void reset(std::uint8_t pattern) noexcept { pattern_ = pattern; }
    [[nodiscard]] std::uint8_t pattern() const noexcept { return pattern_; }

    // One FUN_1000_1349 step: return the retrace-wait count (1 or 2) for this frame
    // and rotate the pattern right (old low bit -> bit 7).
    int step() noexcept {
        const int waits = (pattern_ & 1U) != 0U ? 2 : 1;
        pattern_ = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(pattern_ >> 1U) |
            static_cast<std::uint8_t>((pattern_ & 1U) << 7U));
        return waits;
    }

private:
    std::uint8_t pattern_{0xff};  // EASY until reset
};

}  // namespace bumpy

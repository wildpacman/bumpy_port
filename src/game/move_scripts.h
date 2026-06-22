#pragma once

#include <cstdint>

namespace bumpy {

// One keyframe of a player move script: a sprite frame plus a per-frame pixel
// translation. The ball moves by playing these scripts, not by physics
// (FUN_1000_13df steps one MoveStep per frame, negating dx when facing/mirror is
// set). Frame 100 (0x64) is the "hidden" sentinel the blitter skips. See
// analysis/specs/move-scripts.md and analysis/specs/game-loop.md.
struct MoveStep {
    std::int16_t frame{};
    std::int16_t dx{};
    std::int16_t dy{};
};

// A move script armed by FUN_1000_4263 for a player state: `count` steps starting
// at `steps`, with the initial facing `mirror` (non-zero negates dx). A null
// script (count 0, steps nullptr) marks a state that has no entry in the
// DS:0x2252 table -- the scriptless states {5, 0xb, 0x1c} and the animation-only
// states (death/clear) driven through the DS:0x43c0 table instead.
struct MoveScript {
    std::uint8_t count{};
    std::uint8_t mirror{};
    const MoveStep* steps{};

    [[nodiscard]] bool present() const noexcept { return count != 0 && steps != nullptr; }
};

// Move script for a player state index (0x00..0x40), baked from the binary's
// DS:0x2252 far-pointer table (move_scripts.gen.cpp). Returns a null script for
// out-of-range indices and table gaps.
[[nodiscard]] const MoveScript& move_script(std::uint8_t index);

// Number of entries in the baked table (state indices 0x00..0x40).
inline constexpr int move_script_table_size = 0x41;

}  // namespace bumpy

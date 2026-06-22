#pragma once

#include <cstdint>

namespace bumpy {

// The two player state-machine dispatch tables, baked verbatim from the binary
// (player_dispatch.gen.cpp via tools/re/dump_player_dispatch.py --cpp). Each entry
// is a near pointer into code segment 0x1000, i.e. FUN_1000_<value>; LevelGame maps
// the values to handlers. See analysis/specs/game-loop.md.
inline constexpr int kPlayerStates = 0x41;  // state indices 0x00..0x40
inline constexpr int kAnimSteps = 0x11;     // 17 step-slots per state (row stride 0x22)

// DS:0x7ca -- decide handler per player state (run by FUN_1000_1e02 when at rest).
extern const std::uint16_t kDecideHandler[kPlayerStates];

// DS:0x43c0 -- animation-step handler per (state, step) (run by FUN_1000_238e while
// a scripted move plays). Indexed [state][DAT_792a].
extern const std::uint16_t kAnimStep[kPlayerStates][kAnimSteps];

}  // namespace bumpy

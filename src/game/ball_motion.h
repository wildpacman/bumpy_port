#pragma once

#include "game/move_scripts.h"

#include <cstdint>

namespace bumpy {

// The player ball's low-level motion registers and the two primitives that drive
// them: arm() (FUN_1000_4263) loads a move script for a state, and step()
// (FUN_1000_13df) plays one keyframe per call, translating the ball. This is the
// kinematic core only -- the per-frame *decisions* (which state to enter from the
// tile under the ball + input) live in the higher-level driver. Field names keep
// the original DS:0x* register in a comment. See analysis/specs/game-loop.md.
//
// The ball moves by keyframe scripts, not physics: each step adds the script
// keyframe's {dx,dy} (dx negated when facing is set) and shows its frame. Cell
// spacing is 40x32 px, so a one-cell roll's script nets +-40 or +-32.
struct BallMotion {
    std::uint8_t state{};         // 792c  player state byte (indexes the dispatch tables)
    std::uint8_t steps_left{};    // 824d  script steps remaining (0 => decide next action)
    std::uint8_t step_index{};    // 792a  animation step counter (indexes the 0x43c0 table)
    std::uint8_t facing{};        // 9bae  facing/mirror flag (non-zero negates dx)
    std::uint8_t substep_lock{};  // 8242  when non-zero, arm()/step() are inhibited
    std::int16_t frame{};         // 824a  current sprite frame (100 = hidden)

    int x{};  // 9290  ball pixel x
    int y{};  // 9292  ball pixel y

    std::uint8_t cell{};      // 856e  current board cell (0..47, = row*8 + col)
    std::uint8_t cell_row{};  // 855c  cell / 8
    std::uint8_t cell_col{};  // 855e  cell % 8

    const MoveScript* script{};  // a1ac  active move script (nullptr => none)

    // FUN_1000_4263: enter `new_state`. Unless `new_state` is scriptless
    // ({5, 0xb, 0x1c}), load its move script (steps_left = count, facing = mirror,
    // cursor reset to the start). Always consumes pending input. Inhibited while
    // substep_lock is set, exactly like the original.
    void arm(std::uint8_t new_state);

    // FUN_1000_13df: play one keyframe. Does nothing while substep_lock is set,
    // when no script steps remain, or in a scriptless state. Otherwise it sets the
    // frame, translates the ball by the keyframe's {dx,dy} (dx negated by facing),
    // advances the cursor, decrements steps_left and updates step_index (reset to 0
    // when the script finishes). Returns step_index (0 when idle/just finished).
    std::uint8_t step();

    // FUN_1000_4906: snap the pixel position to a board cell. Recomputes row/col and
    // sets (x,y) to the cell's DS:0x274 screen slot plus the ball draw offset (+7,+15).
    void set_cell(std::uint8_t new_cell);

    // The keyframe step() will apply next (or nullptr when idle). The active script
    // advances in lockstep with steps_left, so the cursor is count - steps_left.
    [[nodiscard]] const MoveStep* current_step() const;

    // True while a scripted move is in progress (steps remain and not scriptless).
    [[nodiscard]] bool moving() const;
};

// The scriptless player states: arm() sets the state but keeps the running script
// (FUN_1000_4263's {5, 0xb, 0x1c} exclusion). step() is also a no-op in them.
[[nodiscard]] bool is_scriptless_state(std::uint8_t state) noexcept;

}  // namespace bumpy

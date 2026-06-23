#include "game/ball_motion.h"

namespace bumpy {

bool is_scriptless_state(std::uint8_t state) noexcept {
    return state == 0x05 || state == 0x0b || state == 0x1c;
}

const MoveStep* BallMotion::current_step() const {
    if (script == nullptr || steps_left == 0 || steps_left > script->count) {
        return nullptr;
    }
    // The cursor advances in lockstep with steps_left, so the next keyframe is at
    // index count - steps_left (0 right after arm()).
    return &script->steps[script->count - steps_left];
}

bool BallMotion::moving() const {
    return steps_left != 0 && !is_scriptless_state(state);
}

void BallMotion::arm(std::uint8_t new_state) {
    // FUN_1000_4263. The original also clears pending input (DAT_8244 = 0) here; that
    // lives in the driver, which owns input. Inhibited inside a sub-step lock.
    if (substep_lock != 0) {
        return;
    }
    state = new_state;
    if (is_scriptless_state(new_state)) {
        return;  // scriptless states keep the running script
    }
    const MoveScript& s = move_script(new_state);
    script = &s;
    steps_left = s.count;
    facing = s.mirror;
}

std::uint8_t BallMotion::step() {
    // FUN_1000_13df.
    if (substep_lock != 0 || steps_left == 0 || is_scriptless_state(state)) {
        return step_index;
    }
    const MoveStep* keyframe = current_step();
    if (keyframe == nullptr) {
        return step_index;
    }
    frame = keyframe->frame;
    x += facing ? -keyframe->dx : keyframe->dx;
    y += keyframe->dy;
    --steps_left;
    if (steps_left == 0) {
        step_index = 0;
    } else {
        ++step_index;
    }
    return step_index;
}

void BallMotion::set_cell(std::uint8_t new_cell) {
    // FUN_1000_4906. Cell = row*8 + col over the 8x6 board grid.
    cell = new_cell;
    cell_row = static_cast<std::uint8_t>(new_cell >> 3);
    cell_col = static_cast<std::uint8_t>(new_cell - cell_row * 8);
    // DS:0x274 screen-slot table (see bum_cell_position): x = 8 + col*40 for all eight
    // columns (col 7 at 288, the rightmost), rows at 8 + row*32; ball sprite draws at +7,+15.
    const int slot_x = 8 + cell_col * 40;
    const int slot_y = 8 + cell_row * 32;
    x = slot_x + 7;
    y = slot_y + 15;
}

}  // namespace bumpy

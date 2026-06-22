#include "game/level_game.h"

#include "game/player_dispatch.h"
#include "game/tile_reactions.h"

#include <algorithm>

namespace bumpy {
namespace {

// Neighbour-reaction tables, indexed by the neighbour cell's plane-B value
// (DAT_8551). Dumped from BUMPY.UNPACKED.EXE DS:0x4256.. -- see the "neighbour
// reaction tables" note in analysis/specs/game-loop.md. A looked-up code becomes
// the ball's next state (the hop handlers 2634/26a1/270c/2776/1fbe/207d).
constexpr std::uint8_t kNeigh4256[32] = {  // hop up-left (2634)
    0x01, 0x12, 0x01, 0x01, 0x01, 0x12, 0x01, 0x12, 0x21, 0x12, 0x12, 0x12, 0x01, 0x21, 0x12, 0x12,
    0x12, 0x12, 0x12, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kNeigh4276[32] = {  // hop up-right (26a1)
    0x02, 0x13, 0x02, 0x02, 0x02, 0x13, 0x13, 0x02, 0x22, 0x13, 0x13, 0x13, 0x02, 0x22, 0x13, 0x13,
    0x13, 0x13, 0x13, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kNeigh4296[32] = {  // bumper (270c)
    0x08, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x08, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x08, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kNeigh42b6[32] = {  // bumper (2776)
    0x09, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x09, 0x15, 0x15, 0x15,
    0x15, 0x15, 0x15, 0x09, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kNeigh4316[32] = {  // special bumper second cell (1fbe)
    0x1a, 0x34, 0x1a, 0x1a, 0x1a, 0x34, 0x1a, 0x34, 0x34, 0x34, 0x34, 0x34, 0x1a, 0x34, 0x34, 0x34,
    0x34, 0x34, 0x34, 0x1a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kNeigh4336[32] = {  // special bumper second cell (207d)
    0x1b, 0x35, 0x1b, 0x1b, 0x1b, 0x35, 0x35, 0x1b, 0x35, 0x35, 0x35, 0x35, 0x1b, 0x35, 0x35, 0x35,
    0x35, 0x35, 0x35, 0x1b, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kNeigh4356[32] = {  // special bumper first cell (1fbe)
    0x1a, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x1a, 0x38, 0x38, 0x38,
    0x38, 0x38, 0x38, 0x1a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kNeigh4376[32] = {  // special bumper first cell (207d)
    0x1b, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x1b, 0x39, 0x39, 0x39,
    0x39, 0x39, 0x39, 0x1b, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// FUN_1000_2810 fall routing: {newstate, sfx} pairs indexed by the plane-A value
// of the cell above (DAT_79b9). Only the state byte is used here. DS:0x76a.
constexpr std::uint8_t kFallRoute[0x60] = {
    0x03, 0x00, 0x06, 0x40, 0x06, 0x41, 0x06, 0x42, 0x00, 0x00, 0x2b, 0x43, 0x2b, 0x44, 0x06, 0x45,
    0x06, 0x46, 0x06, 0x47, 0x06, 0x48, 0x07, 0x00, 0x06, 0x49, 0x06, 0x4a, 0x0a, 0x24, 0x06, 0x27,
    0x03, 0x33, 0x06, 0x4c, 0x2c, 0x00, 0x06, 0x4d, 0x2b, 0x57, 0x2b, 0x58, 0x06, 0x4e, 0x06, 0x4f,
    0x06, 0x50, 0x03, 0x3f, 0x06, 0x51, 0x06, 0x52, 0x06, 0x53, 0x06, 0x54, 0x2c, 0x55, 0x06, 0x56,
    0x06, 0x00, 0x06, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// FUN_1000_695e held-bump action by tile (DAT_7924). DS:0x3cda.
constexpr std::uint8_t kHeldBump[0x30] = {
    0x00, 0x03, 0x3d, 0x07, 0x00, 0x00, 0x00, 0x0a, 0x0d, 0x10, 0x16, 0x00, 0x1c, 0x20, 0x22, 0x00,
    0x39, 0x2a, 0x00, 0x2c, 0x2d, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x5e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// FUN_1000_6d26 structure trigger by plane-A (DAT_7921). DS:0x4396.
constexpr std::uint8_t kStructTrigger[0x30] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x2c, 0x2d, 0x2e, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x56,
    0x5b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x66, 0x11, 0x71, 0x11, 0x71};

}  // namespace

LevelGame::LevelGame(const BumEntities& board, std::uint8_t lives, std::uint32_t score) {
    // FUN_1000_32b0: copy planes A/B/C (0x00..0x8f) + header (0x90..0x95) into the
    // live grid. FUN_1000_2a78 then derives the runtime header fields.
    constexpr std::size_t kLiveBytes = 0x96;
    std::copy(board.bytes.begin(), board.bytes.begin() + kLiveBytes, grid_.begin());

    std::uint8_t start = grid_[0x90];
    if (start != 0) {
        --start;
    }
    d_8572 = grid_[0x91];
    if (d_8572 != 0) {
        --d_8572;
    }
    d_a0cf = grid_[0x92];
    d_7920 = grid_[0x95];

    d_791a = lives;
    d_a0d4 = static_cast<std::uint16_t>(score & 0xffff);
    d_a0d6 = static_cast<std::uint16_t>(score >> 16);

    ball_.state = 0;  // idle hub
    ball_.set_cell(start);  // FUN_1000_4906: position the ball on its start cell
}

std::uint32_t LevelGame::score() const noexcept {
    return (static_cast<std::uint32_t>(d_a0d6) << 16) | d_a0d4;
}

std::uint8_t LevelGame::collectible(int col, int row) const {
    if (col < 0 || col >= 8 || row < 0 || row >= 6) {
        return 0;
    }
    return grid_[0x60 + static_cast<std::size_t>(row) * 8 + col];
}

std::uint16_t LevelGame::prng_next() {
    // Stand-in for FUN_1000_93b1; only feeds 4747's idle-blink frame choice, so a
    // plain 16-bit LCG (deterministic) is faithful enough.
    prng_state_ = static_cast<std::uint16_t>(prng_state_ * 25173u + 13849u);
    d_79b3 = static_cast<std::uint8_t>(prng_state_ >> 8);
    return d_79b3;
}

std::uint8_t LevelGame::build_input_bits(const LevelInput& in) const {
    std::uint8_t bits = 0;
    if (in.left) bits |= 0x01;
    if (in.right) bits |= 0x02;
    if (in.up) bits |= 0x04;
    if (in.down) bits |= 0x08;
    if (in.fire) bits |= 0x10;
    return bits;
}

void LevelGame::tick(const LevelInput& input) {
    prng_next();   // FUN_1000_93b1
    ball_.step();  // FUN_1000_13df: advance the active move script
    f_1d26(input); // player tick

    if (d_928d) {
        status_ = LevelStatus::quit;
    } else if (d_9d30) {
        status_ = LevelStatus::dead;
    } else if (d_856d || d_a1b1) {
        // a1b1 is set the moment the last required collectible is taken (the win
        // cascade); the port treats that as the win without requiring the exit tile.
        status_ = LevelStatus::won;
    } else {
        status_ = LevelStatus::playing;
    }
}

void LevelGame::f_1d26(const LevelInput& input) {
    // Function keys (F1-F7) are not modelled here.
    if (d_a1aa != 0) {
        f_228d();
        return;
    }
    f_236f();
    f_1dde(input);
    if (ball_.steps_left == 0) {
        f_1e02();
    } else {
        f_238e();
    }
}

void LevelGame::f_236f() { d_7924 = grid_[ball_.cell]; }

void LevelGame::f_1dde(const LevelInput& input) {
    const std::uint8_t bits = build_input_bits(input);
    if (bits != 0) {
        d_8244 = bits;  // sticky: keep the last non-zero action
    }
}

void LevelGame::f_4263(std::uint8_t new_state) {
    d_8244 = 0;          // FUN_1000_4263 always consumes input
    ball_.arm(new_state);
}

void LevelGame::f_4906() { ball_.set_cell(ball_.cell); }

void LevelGame::f_6bb5(std::uint8_t cell) { d_7921 = grid_[cell]; }
void LevelGame::f_6bd4(std::uint8_t cell) { d_8551 = grid_[cell + 0x30]; }
void LevelGame::f_6bf4(std::uint8_t cell) { d_79b8 = grid_[cell + 0x60]; }

void LevelGame::f_1e02() {
    d_7923 = 0;
    d_8552 = ball_.state;
    if (d_a0ce == 0 && d_a1a7 != 0) {
        f_27de();
    } else {
        decide_dispatch(ball_.state);
    }
}

void LevelGame::f_238e() { anim_dispatch(ball_.state, ball_.step_index); }

void LevelGame::decide_dispatch(std::uint8_t state) {
    switch (kDecideHandler[state]) {
    case 0x28f9: f_28f9(); break;
    case 0x23b6: f_23b6(); break;
    case 0x2470: f_2470(); break;
    case 0x248e: f_248e(); break;
    case 0x24d7: f_24d7(); break;
    case 0x250a: f_250a(); break;
    case 0x25ad: f_25ad(); break;
    case 0x22b0: f_22b0(); break;
    case 0x2423: f_2423(); break;
    case 0x4344: f_4344(); break;
    case 0x4437: f_4437(); break;
    case 0x2810: f_2810(); break;
    case 0x22c1: f_22fc(); break;
    case 0x22d2: f_228d(); break;  // death cascade (board 0: unreached)
    default: break;                // anim-only / death states: no rest decision
    }
}

void LevelGame::anim_dispatch(std::uint8_t state, std::uint8_t step) {
    if (step >= kAnimSteps) {
        return;
    }
    const std::uint8_t col = ball_.cell_col;
    switch (kAnimStep[state][step]) {
    case 0x64e2: d_8244 &= 0x0f; ball_.cell -= 8; ball_.cell_row -= 1; break;  // UP a row
    case 0x64ff: d_8244 &= 0x0f; ball_.cell += 8; ball_.cell_row += 1; break;  // DOWN a row
    case 0x651c: d_8244 &= 0x0f; ball_.cell -= 1; ball_.cell_col -= 1; break;  // LEFT a col
    case 0x6535: d_8244 &= 0x0f; ball_.cell += 1; ball_.cell_col += 1; break;  // RIGHT a col
    case 0x6611: d_8244 &= 0x0f; break;
    case 0x65e5: d_8244 &= 0x10; break;
    case 0x65fb: d_8244 &= 0x1d; break;
    case 0x65d2: d_8244 = 0; break;
    case 0x6717: f_6717(); break;
    case 0x654e: f_654e(); break;
    case 0x6587: f_6587(); break;
    case 0x6627: f_6627(); break;
    // input-clearing entry/step handlers (sound elided); some are column-gated.
    case 0x68fe: case 0x693a: case 0x67e2: case 0x6813: case 0x6832: case 0x684b:
        d_8244 = 0; break;
    case 0x68e6: case 0x6890: case 0x67ca: if (col != 0) d_8244 = 0; break;
    case 0x6922: case 0x68bb: case 0x67fb: if (col != 7) d_8244 = 0; break;
    default: break;  // 0x7111 filler + cosmetic sprite/sfx entry handlers
    }
}

// ---- decide handlers ---------------------------------------------------------

void LevelGame::f_28f9() {
    d_824c = 8;
    if (d_79b4 == 0 && d_7924 != 0) {
        if (d_7924 == 0x16) {
            ball_.state = 0x1c;  // pipe-enter (FUN_1000_4305); not present in world 1
        } else if (d_7924 == 0x03) {
            // FUN_1000_463d settle: a 3-frame delay (using the sub-step lock as the
            // counter, which also inhibits motion meanwhile) then re-decide.
            if (++ball_.substep_lock == 3) {
                ball_.substep_lock = 0;
                f_2965();
            }
        } else {
            f_2965();
        }
    } else {
        d_79b4 = 0;
        if (ball_.cell < 0x28) {
            f_28e0();
        } else {
            f_42d9();
        }
    }
}

void LevelGame::f_2965() {
    if (d_8244 & 0x04) {
        f_467d();
    } else if (d_8244 & 0x08) {
        f_469c();
    } else if (d_7924 == 0x0a) {
        f_47cb();
    } else if (d_7924 == 0x0f) {
        f_4802();
    } else {
        f_29a6();
    }
}

void LevelGame::f_29a6() {
    if (ball_.cell < 8) {
        f_465e();
        return;
    }
    d_856f = static_cast<std::uint8_t>(ball_.cell - 8);
    if (grid_[d_856f] == 0x0e) {
        if ((d_8244 & 0x02) == 0) {
            f_4263(0x0a);
        } else {
            f_253f();
        }
    } else {
        f_465e();
    }
}

void LevelGame::f_465e() { f_46bb(tile_reaction_code(TileReactionTable::none, d_7924)); }
void LevelGame::f_467d() { f_46bb(tile_reaction_code(TileReactionTable::up, d_7924)); }
void LevelGame::f_469c() { f_46bb(tile_reaction_code(TileReactionTable::down, d_7924)); }

void LevelGame::f_46bb(std::uint8_t code) {
    if (code == 8) {
        f_270c();
        return;
    }
    if (code < 9) {
        switch (code) {
        case 0: f_4747(); break;
        case 1: f_2634(); break;
        case 2: f_26a1(); break;
        case 3: f_27de(); break;
        default: f_472d(code); break;
        }
    } else if (code == 9) {
        f_2776();
    } else if (code == 0x1a) {
        f_1fbe();
    } else if (code == 0x1b) {
        f_207d();
    } else {
        f_472d(code);
    }
}

void LevelGame::f_472d(std::uint8_t code) {
    f_4263(code);
    f_238e();
}

void LevelGame::f_4747() {
    std::uint8_t code = 0;
    if (ball_.cell >= 8) {
        code = tile_reaction_code(TileReactionTable::roll_above, grid_[ball_.cell - 8]);
    }
    if (code == 0) {
        if (d_79b3 >= 0xec) {
            code = 0x3c;
        } else if (d_79b3 >= 0xd8) {
            code = 0x3d;
        } else if (d_79b3 >= 0xc4) {
            code = 0x3e;
        } else if (d_79b3 > 0xaf) {
            code = 0x3f;
        }
    }
    f_472d(code);
}

void LevelGame::f_47cb() {
    if (d_8244 & 0x04) {
        f_2634();
    } else if (d_8244 & 0x08) {
        f_26a1();
    } else {
        f_46bb(tile_reaction_code(TileReactionTable::on_0a, ball_.state));
    }
}

void LevelGame::f_4802() {
    d_856f = ball_.cell;
    f_4263(0x0e);  // -> warp state
    f_238e();
}

void LevelGame::f_2634() {  // hop up-left
    d_8551 = 0;
    std::uint8_t code;
    if (ball_.cell_col == 0) {
        d_8551 = 0x1f;
        code = 0x12;
    } else {
        d_8570 = static_cast<std::uint8_t>(ball_.cell - 1);
        f_6bd4(d_8570);
        code = kNeigh4256[d_8551 & 0x1f];
        if (code == 1) {
            f_6bb5(d_8570);
            code = (d_7921 == 0x0b) ? 0x16 : 1;
        }
    }
    f_4263(code);
    f_238e();
}

void LevelGame::f_26a1() {  // hop up-right
    d_8551 = 0;
    std::uint8_t code;
    if (ball_.cell_col == 7) {
        d_8551 = 0x1f;
        code = 0x13;
    } else {
        d_8570 = ball_.cell;
        f_6bd4(ball_.cell);
        code = kNeigh4276[d_8551 & 0x1f];
        if (code == 2) {
            f_6bb5(static_cast<std::uint8_t>(ball_.cell + 1));
            code = (d_7921 == 0x0b) ? 0x17 : 2;
        }
    }
    f_4263(code);
    f_238e();
}

void LevelGame::f_270c() {
    d_8551 = 0;
    std::uint8_t code;
    if (ball_.cell_col == 0) {
        d_8551 = 0x1f;
        code = 0x14;
    } else {
        d_8570 = static_cast<std::uint8_t>(ball_.cell - 1);
        f_6bd4(d_8570);
        code = kNeigh4296[d_8551 & 0x1f];
        if (code == 8) {
            f_6bb5(d_8570);
            code = (d_7921 == 0x0b) ? 0x18 : 8;
        }
    }
    f_4263(code);
    f_238e();
}

void LevelGame::f_2776() {
    d_8551 = 0;
    std::uint8_t code;
    if (ball_.cell_col == 7) {
        d_8551 = 0x1f;
        code = 0x15;
    } else {
        d_8570 = ball_.cell;
        f_6bd4(ball_.cell);
        code = kNeigh42b6[d_8551 & 0x1f];
        if (code == 9) {
            f_6bb5(static_cast<std::uint8_t>(ball_.cell + 1));
            code = (d_7921 == 0x0b) ? 0x19 : 9;
        }
    }
    f_4263(code);
    f_238e();
}

void LevelGame::f_27de() {
    d_7923 = 0;
    d_a1a7 = 0;
    d_79b9 = 0x0b;
    if (d_7924 == 0x11) {
        f_4263(0x2f);
    } else {
        f_2810();
    }
    f_238e();
}

void LevelGame::f_2810() {
    if (ball_.cell < 8) {
        f_4263(6);
    } else {
        d_856f = static_cast<std::uint8_t>(ball_.cell - 8);
        d_79b9 = grid_[d_856f];
        f_4263(d_79b9 < 0x30 ? kFallRoute[d_79b9 * 2] : 6);
    }
}

void LevelGame::f_23b6() {  // rolling
    if ((d_8244 & 0x04) == 0) {
        if ((d_8244 & 0x08) == 0) {
            const bool above_not_0e = ball_.cell < 8 || grid_[ball_.cell - 8] != 0x0e;
            if (above_not_0e && (d_8244 & 0x02)) {
                f_4747();
            } else {
                f_27de();
            }
        } else {
            f_26a1();
        }
    } else {
        f_2634();
    }
}

void LevelGame::f_2470() {
    d_824c = 8;
    f_4263(0x0b);
    f_238e();
}

void LevelGame::f_248e() {
    if (d_8244 & 0x02) {
        f_4263(0x0c);
        f_238e();
    }
}

void LevelGame::f_24d7() {
    if (d_7924 == 0 && grid_[ball_.cell + 8] != 0x0b) {
        f_28e0();
    } else {
        f_250a();
    }
}

void LevelGame::f_250a() {
    if ((d_8244 & 0x04) == 0) {
        if ((d_8244 & 0x08) == 0) {
            if ((d_8244 & 0x02) == 0) {
                f_27de();
            } else {
                f_253f();
            }
        } else {
            f_26a1();
        }
    } else {
        f_2634();
    }
}

void LevelGame::f_253f() {
    if (d_7924 == 0x0f) {
        f_4802();
    } else if (d_7924 == 0x12 || d_7924 == 0x1f) {
        f_22b0();
    }
    d_8244 &= 0x1d;
    if (++d_824c == 9) {
        d_856f = static_cast<std::uint8_t>(ball_.cell - 8);
        d_824c = 0;
    }
    f_4263(0x0d);
    f_238e();
}

void LevelGame::f_28e0() {
    f_4263(4);
    f_238e();
}

void LevelGame::f_42d9() {
    f_4263(0x2d);
    f_238e();
}

void LevelGame::f_25ad() {  // warp: find the next hole and pop out of it
    std::uint8_t c = ball_.cell;
    for (int guard = 0; guard < 0x30; ++guard) {
        c = static_cast<std::uint8_t>(c + 1);
        if (c == 0x30) {
            c = 0;
        }
        if (grid_[c] == 0x0f) {
            d_856f = c;
            ball_.cell = c;
            f_4906();
            ball_.y += 0xd;
            f_4263(0x0f);
            f_238e();
            return;
        }
    }
}

void LevelGame::f_22b0() { f_22fc(); }

void LevelGame::f_22fc() {
    d_a0ce = 0;
    d_856d = 1;  // win / leave board
    if (d_791a == 0) {
        d_928d = 0xff;
    } else {
        --d_791a;
    }
}

void LevelGame::f_2423() {  // bounce (scriptless state 5)
    if (d_8244 & 0x04) {
        f_2634();
    } else if (d_8244 & 0x08) {
        f_26a1();
    }
}

void LevelGame::f_1fbe() {  // special bumper, left
    d_8551 = 0;
    std::uint8_t code;
    if (ball_.cell_col == 0) {
        d_8551 = 0x1f;
        code = 0x38;
    } else {
        d_8570 = static_cast<std::uint8_t>(ball_.cell - 1);
        f_6bd4(d_8570);
        if (kNeigh4356[d_8551 & 0x1f] == 0x38) {
            code = 0x38;
        } else {
            f_6bb5(static_cast<std::uint8_t>(ball_.cell - 1));
            if (d_7921 == 0x0b) {
                code = 0x3a;
            } else if (ball_.cell_col == 1) {
                d_8551 = 0x1f;
                code = 0x34;
            } else {
                d_8570 = static_cast<std::uint8_t>(ball_.cell - 2);
                f_6bd4(d_8570);
                if (kNeigh4316[d_8551 & 0x1f] == 0x34) {
                    code = 0x34;
                } else {
                    f_6bb5(static_cast<std::uint8_t>(ball_.cell - 2));
                    code = (d_7921 == 0x0b) ? 0x36 : 0x1a;
                }
            }
        }
    }
    f_4263(code);
    f_238e();
}

void LevelGame::f_207d() {  // special bumper, right
    d_8551 = 0;
    std::uint8_t code;
    if (ball_.cell_col == 7) {
        d_8551 = 0x1f;
        code = 0x39;
    } else {
        d_8570 = ball_.cell;
        f_6bd4(ball_.cell);
        if (kNeigh4376[d_8551 & 0x1f] == 0x39) {
            code = 0x39;
        } else {
            f_6bb5(static_cast<std::uint8_t>(ball_.cell + 1));
            if (d_7921 == 0x0b) {
                code = 0x3b;
            } else if (ball_.cell_col == 6) {
                d_8551 = 0x1f;
                code = 0x35;
            } else {
                d_8570 = static_cast<std::uint8_t>(ball_.cell + 1);
                f_6bd4(d_8570);
                if (kNeigh4336[d_8551 & 0x1f] == 0x35) {
                    code = 0x35;
                } else {
                    f_6bb5(static_cast<std::uint8_t>(ball_.cell + 2));
                    code = (d_7921 == 0x0b) ? 0x37 : 0x1b;
                }
            }
        }
    }
    f_4263(code);
    f_238e();
}

void LevelGame::f_228d() {  // entity hit -> death (board 0: unreached)
    d_a0ce = 1;
    ball_.step_index = 0;
    d_a1aa = 0;
    f_4263(0x2e);
}

// ---- helpers -----------------------------------------------------------------

void LevelGame::f_6717() {
    d_856f = ball_.cell;
    f_6d26();
}

void LevelGame::f_6d26() {
    f_6bb5(ball_.cell);
    const std::uint8_t trigger = d_7921 < 0x30 ? kStructTrigger[d_7921] : 0;
    if (trigger != 0) {
        d_7922 = d_7924;
        d_7923 = trigger;
        // FUN_1000_6d94 only plays a sound; no state change.
    }
}

void LevelGame::f_654e() {
    if (d_7923 == 0 && ((d_8244 & 0x10) != 0 || (d_8244 & 0x01) != 0)) {
        d_7922 = d_7924;
        f_695e(d_7924 < 0x30 ? kHeldBump[d_7924] : 0);
    }
}

void LevelGame::f_695e(std::uint8_t action) {
    d_856f = ball_.cell;
    if (action != 0) {
        d_a1a7 = action;
    }
}

void LevelGame::f_6587() {
    if (d_a1a7 == 0 && d_7924 == 0x02 && (d_8244 & 0x02)) {
        d_856f = ball_.cell;
        d_79b4 = 0x34;
    }
}

void LevelGame::f_6627() {
    f_6bf4(ball_.cell);
    if (d_79b8 != 0) {
        f_6c14();
    }
}

void LevelGame::f_6c14() {
    f_6c95();
    grid_[ball_.cell + 0x60] = 0;  // remove the collectible
    if (d_79b8 != 0x01 && d_79b8 != 0x23) {
        --d_a0cf;
        if (d_a0cf == 0) {
            d_856f = d_8572;
            d_a1b1 = 1;  // win cascade armed
            d_8550 = 0xf2;
        }
    }
}

void LevelGame::f_6c95() {
    const std::uint16_t bumped = static_cast<std::uint16_t>(d_a0d4 + 0xfa);
    if (d_a0d4 > 0xff05) {
        ++d_a0d6;
    }
    if (d_79b8 == 0x23) {  // '#' extra life
        ++d_791a;
        d_a0d4 = bumped;
    } else if (d_79b8 == 0x2f) {  // '/' +10000
        const bool carry = bumped > 0xd9e9;
        d_a0d4 = static_cast<std::uint16_t>(d_a0d4 + 10000);
        if (carry) ++d_a0d6;
    } else if (d_79b8 == 0x30) {  // '0' +50000
        const bool carry = bumped > 0x3da9;
        d_a0d4 = static_cast<std::uint16_t>(d_a0d4 + 50000);
        if (carry) ++d_a0d6;
    } else {  // +250 base
        d_a0d4 = bumped;
    }
}

// ---- input-decode tree (the 0x43c0 chaining) ---------------------------------

void LevelGame::f_4437() {
    if (d_8244 & 0x10) {
        f_440c();
    } else {
        f_4398();
    }
}

void LevelGame::f_4344() {
    if (d_8244 & 0x10) {
        // FUN_1000_431b: fire on a pipe tile; world 1 has none, so just hop on dir.
        if (d_8244 & 0x04) {
            f_2634();
        } else if (d_8244 & 0x08) {
            f_26a1();
        }
    } else {
        f_4398();
    }
}

void LevelGame::f_4398() {
    if (d_8244 & 0x01) {
        f_4454();
    } else {
        f_43b5();
    }
}

void LevelGame::f_43b5() {
    if (d_8244 & 0x02) {
        f_448a();
    } else {
        f_43d2();
    }
}

void LevelGame::f_43d2() {
    if (d_8244 & 0x04) {
        f_44c0();
    } else {
        f_43ef();
    }
}

void LevelGame::f_43ef() {
    if (d_8244 & 0x08) {
        f_4532();
    } else {
        f_440c();
    }
}

void LevelGame::f_440c() {
    d_856f = ball_.cell;
    f_236f();
    if (d_7924 == 0x16) {
        ball_.state = 0x1c;  // pipe-enter; not in world 1
    }
    // else: no direction -> stay at rest (the original only queues a settle sfx).
}

void LevelGame::f_4454() {  // LEFT -> hop UP if the cell above is clear
    if (ball_.cell < 8) {
        f_43b5();
    } else if (!f_45cf(static_cast<std::uint8_t>(ball_.cell - 8))) {
        f_45a0(0x1d);
    } else {
        f_43b5();
    }
}

void LevelGame::f_448a() {  // RIGHT -> hop DOWN if the cell below is clear
    if (ball_.cell < 0x28) {
        if (!f_45cf(static_cast<std::uint8_t>(ball_.cell + 8))) {
            f_45a0(0x1e);
        } else {
            f_43d2();
        }
    } else {
        f_43d2();
    }
}

void LevelGame::f_44c0() {  // UP -> hop up-left if the left cell is occupied
    if (ball_.cell_col == 0) {
        f_43ef();
    } else if (f_45cf(static_cast<std::uint8_t>(ball_.cell - 1))) {
        f_450c();
    } else if (f_4605(static_cast<std::uint8_t>(ball_.cell - 1))) {
        f_450c();
    } else {
        f_45a0(0x1f);
    }
}

void LevelGame::f_4532() {  // DOWN -> hop up-right if the right cell is occupied
    if (ball_.cell_col == 7) {
        f_440c();
    } else if (f_45cf(static_cast<std::uint8_t>(ball_.cell + 1))) {
        f_457a();
    } else if (f_4605(ball_.cell)) {
        f_457a();
    } else {
        f_45a0(0x20);
    }
}

void LevelGame::f_450c() {
    if (d_7924 != 0x16) {
        d_856f = ball_.cell;
    }
    f_2634();
}

void LevelGame::f_457a() {
    if (d_7924 != 0x16) {
        d_856f = ball_.cell;
    }
    f_26a1();
}

void LevelGame::f_45a0(std::uint8_t state) {
    f_4263(state);
    if (d_7924 != 0) {
        d_856f = ball_.cell;
    }
    f_238e();
}

bool LevelGame::f_45cf(std::uint8_t cell) {
    const std::uint8_t v = grid_[cell];
    return v != 0 && v != 0x19;
}

bool LevelGame::f_4605(std::uint8_t cell) {
    const std::uint8_t v = grid_[cell + 0x30];
    return v != 0 && v != 0x13;
}

}  // namespace bumpy

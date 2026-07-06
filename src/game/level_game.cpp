#include "game/level_game.h"

#include "game/player_dispatch.h"
#include "game/tile_reactions.h"
#include "resources/sfx_tables.h"

#include <algorithm>
#include <utility>

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

// FUN_1000_2810 fall routing: {newstate, springId} pairs indexed by the plane-A
// value of the cell above (DAT_79b9). The state byte arms the fall; the second
// byte (non-zero) is a layer-A spring event passed to FUN_1000_69aa. DS:0x76a.
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

// FUN_1000_2138/21e7 block-top walk: the next state by the neighbour cell's
// plane-B value (DS:0x42d6 walking left / 0x42f6 walking right). The 0x25/0x26
// sentinels re-check plane A via FUN_1000_21bb/2261; 0x27/0x28 hop off the edge.
constexpr std::uint8_t k42d6[32] = {
    0x25, 0x27, 0x25, 0x25, 0x25, 0x27, 0x25, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 0x27,
    0x27, 0x27, 0x27, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t k42f6[32] = {
    0x26, 0x28, 0x26, 0x26, 0x26, 0x28, 0x28, 0x26, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
    0x28, 0x28, 0x28, 0x26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// FUN_1000_495c ball-frame cycles: DS:0x1b70 the nest spin (FUN_1000_4361, wrap
// 0x15 every 4 frames), DS:0x1ca4/0x1cba the cushion bob (states 0x23/0x24, wrap
// 0x0b every 5 frames). Values are the ball's bank frame.
constexpr std::int16_t kNestSpinFrames[0x15] = {
    7, 6, 6, 5, 5, 6, 6, 7, 0, 1, 2, 2, 3, 3, 2, 2, 1, 0, 0, 0, 0};
constexpr std::int16_t kCushionBobL[0x0b] = {8, 9, 0xa, 0xb, 0xa, 0xa, 9, 0xa, 0xa, 0xa, 0xa};
constexpr std::int16_t kCushionBobR[0x0b] = {8, 9, 0xa, 0xb, 0xa, 0xa, 9, 0xa, 0xa, 0xa, 0xa};

// Raw move scripts armed OUTSIDE the DS:0x2252 state table (the original writes
// a1ac/824d/9bae/792a directly). kEntryDrop = the board-entry drop-in
// (FUN_1000_31de, DS:0x1394: the ball materializes 12px above its cell and
// settles); kCushionRoll* = rolling off a cushion block (FUN_1000_1f03/1f7f,
// DS:0x140c/0x1460, reusing the roll states 1/2).
constexpr MoveStep kEntryDropSteps[10] = {
    {0, 0, -1}, {0, 0, -1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 1},
    {0, 0, 1},  {0, 0, 2},  {0, 0, 3}, {0, 0, 3}, {0, 0, 4}};
constexpr MoveScript kEntryDrop{10, 0, kEntryDropSteps};
constexpr MoveStep kCushionRollLSteps[4] = {{7, 4, 2}, {7, 2, 3}, {0, 2, 3}, {0, 2, 4}};
constexpr MoveScript kCushionRollL{4, 0, kCushionRollLSteps};
constexpr MoveStep kCushionRollRSteps[4] = {{1, 4, 2}, {1, 2, 3}, {0, 2, 3}, {0, 2, 4}};
constexpr MoveScript kCushionRollR{4, 0, kCushionRollRSteps};

// FUN_1000_6d26 structure trigger by plane-A (DAT_7921). DS:0x4396.
constexpr std::uint8_t kStructTrigger[0x30] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x2c, 0x2d, 0x2e, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x56,
    0x5b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x66, 0x11, 0x71, 0x11, 0x71};

// --- moving-entity movement scripts + sprite bases ---
// Generated by tools/re/dump_entity_ai.py from BUMPY.UNPACKED.EXE (DS:0x2520 /
// DS:0x2546). Keyframes are {frame, dx, dy}, one stepped per active frame (4c14);
// ids 1=up 2=down 3=left 4=right, 5..8 are the stuck-fallback bobs, 9 a long idle.
struct EntityKeyframe { std::int16_t frame, dx, dy; };
struct EntityScript { std::uint8_t kf; std::uint8_t count; std::uint8_t dir; };
constexpr EntityKeyframe kEntityKeyframes[88] = {
    {0, 0, -4}, {0, 0, -4}, {1, 0, -4}, {1, 0, -4}, {2, 0, -4}, {2, 0, -4}, {3, 0, -4}, {3, 0, -4},
    {0, 0, 4}, {0, 0, 4}, {1, 0, 4}, {1, 0, 4}, {2, 0, 4}, {2, 0, 4}, {3, 0, 4}, {3, 0, 4},
    {1, 4, 0}, {1, 4, 0}, {2, 4, 0}, {2, 4, 0}, {1, 4, 0}, {1, 4, 0}, {2, 4, 0}, {2, 4, 0},
    {3, 4, 0}, {3, 4, 0}, {1, 4, 0}, {1, 4, 0}, {2, 4, 0}, {2, 4, 0}, {1, 4, 0}, {1, 4, 0},
    {2, 4, 0}, {2, 4, 0}, {3, 4, 0}, {3, 4, 0}, {1, 0, -3}, {1, 0, -2}, {2, 0, 0}, {2, 0, 3},
    {1, 0, 3}, {1, 0, -3}, {2, 0, -3}, {3, 0, 2}, {0, 0, 3}, {1, 0, 3}, {1, 0, 2}, {2, 0, 0},
    {3, 0, -3}, {2, 0, -3}, {1, 0, 3}, {0, 0, 3}, {1, 0, -2}, {0, 0, -3}, {0, 3, 0}, {1, 3, 0},
    {2, 2, 0}, {3, 0, 0}, {2, -4, 0}, {1, -3, 0}, {0, -2, 0}, {1, -1, 0}, {2, 0, 0}, {3, 2, 0},
    {0, 3, 0}, {1, 3, 0}, {2, 2, 0}, {3, 0, 0}, {2, -4, 0}, {1, -3, 0}, {0, -2, 0}, {1, -1, 0},
    {2, 0, 0}, {3, 2, 0}, {1, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 0, 0}, {1, 0, 0}, {1, 0, 0},
    {2, 0, 0}, {2, 0, 0}, {3, 0, 0}, {3, 0, 0}, {2, 0, 0}, {2, 0, 0}, {3, 0, 0}, {0, 0, 0}};
constexpr EntityScript kEntityScripts[10] = {
    {0, 0, 0},    // id 0: null/idle
    {0, 8, 0},   {8, 8, 0},   {16, 10, 1}, {26, 10, 0}, {36, 9, 0},
    {45, 9, 0},  {54, 10, 1}, {64, 10, 0}, {74, 14, 0}};
constexpr std::uint16_t kEntityAnimBase[18] = {
    0x103b, 0x015a, 0x015e, 0x0162, 0x0166, 0x016a, 0x016e, 0x0172, 0x0176,
    0x01db, 0x01df, 0x01e3, 0x01e7, 0x01eb, 0x01ef, 0x01f3, 0x01f7, 0x01fb};

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

    // FUN_1000_31de (via the board-entry FUN_1000_0bf9): the ball drops in from
    // 12px above its cell over the raw 10-step DS:0x1394 script (step_index 4 so
    // the state-0 anim row's tail runs during the drop), and the picture-block
    // cascade list starts empty.
    ball_.y -= 12;
    ball_.script = &kEntryDrop;
    ball_.steps_left = kEntryDrop.count;
    ball_.facing = 4;
    ball_.step_index = 4;
    d_0886_[0] = 0xff;

    // Moving entity (monster). FUN_1000_2a78: cell from header 0x93 (value-1; a 0
    // byte wraps to 0xff = "no entity"), behaviour id from 0x94, sprite-frame base
    // from 0x96 via the DS:0x2546 table. Most boards have none (0x93 == 0).
    d_8571 = static_cast<std::uint8_t>(grid_[0x93] - 1);
    const std::uint8_t anim_idx = board.bytes[0x96];
    d_a0de = anim_idx < 18 ? kEntityAnimBase[anim_idx] : 0;
    f_48a9();                 // position the entity (no-op when 8571 == 0xff)
    f_4bc6(grid_[0x94]);      // load the first movement script (sets d_8562)
}

std::uint32_t LevelGame::score() const noexcept {
    return (static_cast<std::uint32_t>(d_a0d6) << 16) | d_a0d4;
}

std::vector<std::uint8_t> LevelGame::take_sfx_events() {
    std::vector<std::uint8_t> events = std::move(pending_sfx_);
    pending_sfx_.clear();  // guarantee the moved-from queue is empty regardless of stdlib
    return events;
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
    // Action-bit assignment recovered from FUN_1000_75a2 / FUN_1000_773c (the joystick
    // read) and confirmed by the 0x43c0 input-decode tree (4437): bit 0x01 = UP (hop,
    // state 0x1d), 0x02 = DOWN (state 0x1e), 0x04 = LEFT (up-left, 2634), 0x08 = RIGHT
    // (up-right, 26a1). The earlier left=0x01/up=0x04 guess transposed the axes, which
    // showed up in-game as a 90-degree-rotated control scheme.
    std::uint8_t bits = 0;
    if (in.up) bits |= 0x01;
    if (in.down) bits |= 0x02;
    if (in.left) bits |= 0x04;
    if (in.right) bits |= 0x08;
    if (in.fire) bits |= 0x10;
    return bits;
}

void LevelGame::tick(const LevelInput& input) {
    prng_next();   // FUN_1000_93b1
    ball_.step();  // FUN_1000_13df: advance the active move script
    f_4c14();      // FUN_1000_4c14: step the entity's movement script (every other frame)
    f_14e4();      // step the layer-A peg/spring animations
    f_15a1();      // step the layer-B block/spring animations
    f_5085();      // FUN_1000_5085: build the ball AABB collision box
    f_50c0();      // FUN_1000_50c0: build the entity AABB collision box
    f_629c();      // FUN_1000_629c: pop the next matched-puzzle 0x05 block, if any
    f_1d26(input); // player tick (consumes a1aa from last frame -> death; may arm springs)
    f_4c99();      // FUN_1000_4c99: entity maze AI -- pick the next direction on arrival
    f_50fb();      // FUN_1000_50fb: ball-vs-entity overlap -> a1aa (death next frame)
    f_233a();      // FUN_1000_233a: pulse the exit portal once it has opened

    if (d_928d) {
        status_ = LevelStatus::quit;
    } else if (d_9d30) {
        // FUN_1000_1e3d marked the world-map node cleared: the ball fell into the
        // exit portal (state 0x30 descent finished). This is the real board clear.
        status_ = LevelStatus::won;
    } else if (d_856d) {
        // FUN_1000_22fc: the board ended by losing a life -- enemy death, a deadly
        // pit (chute tiles 0x12/0x1f), or the F2 skip. Board 0 has none of these.
        status_ = LevelStatus::dead;
    } else {
        // a1b1 only *arms* the exit (the last required collectible was taken); the
        // ball must still roll to the pit and fall in before the board is cleared.
        status_ = LevelStatus::playing;
    }
}

void LevelGame::f_1d26(const LevelInput& input) {
    // FUN_1000_1d26 polls the function keys first (F1-F5 -> debug DAT_854f; F10 ->
    // DAT_928d = 1 hard quit) and, between them, scancode 0x01 (Escape) -> FUN_1000_22fc,
    // then falls through (jmp 0x1dbb) into the state machine below. Only Escape is a
    // player-facing key, so it is the only one modelled: it loses a life (DAT_856d = 1;
    // DAT_791a--, or DAT_928d = 0xff on the last life), ending the board like a death.
    // f_22fc only fires once per board because tick() reports the terminal status right
    // after this and the shell tears the board down. (Escape is NOT in the 75a2 action
    // mask, so it never satisfies the board-start pause FUN_1000_328f -- matching the
    // original, where Escape at the start does nothing until play begins.)
    if (input.cancel) {
        f_22fc();
    }
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

void LevelGame::f_233a() {
    // FUN_1000_233a: once the exit portal is open (a1b1), keep its idle pulse going --
    // every 10 frames re-arm event 0x5a (the pit's bob) at the portal cell. d_8550 was
    // seeded to 0xf2 in f_6c14, so the first pulse follows the one-shot 0x59 opening.
    if (d_a1b1 != 0) {
        if (d_8550 == 9) {
            d_8550 = 0;
            d_856f = d_8572;
            f_69aa(0x5a);
        } else {
            ++d_8550;
        }
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
    case 0x1e3d: f_1e3d(); break;  // state 0x30: the portal descent finished -> cleared
    case 0x22b0: f_22b0(); break;
    case 0x2423: f_2423(); break;
    case 0x4344: f_4344(); break;
    case 0x4437: f_4437(); break;
    case 0x2810: f_2810(); break;
    case 0x22c1: f_22fc(); break;
    case 0x22d2: f_22d2(); break;  // death-tumble cascade (FUN_1000_22d2)
    // Block-top riding (worlds 2+): landing on / walking along plane-B blocks.
    case 0x1e5e: f_1e5e(); break;  // state 0x21: landed from a hop up-left
    case 0x1e90: f_1e90(); break;  // state 0x22: landed from a hop up-right
    case 0x1ec2: f_1ec2(); break;  // state 0x23: sitting on a cushion block
    case 0x1f3e: f_1f3e(); break;  // state 0x24
    case 0x2138: f_2138(); break;  // state 0x25: walking left on block tops
    case 0x21e7: f_21e7(); break;  // state 0x26: walking right on block tops
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
    // FUN_1000_647e is a bump sound (kSfxHeldBump[the fallen-from cell's plane-A,
    // DAT_79b9]) followed by FUN_654e, the held-bump latch. It is the step-4/5 handler
    // of the bounce states 0x06/0x07/0x2b; without the 654e routing a held UP only
    // re-armed the bounce every other cycle, so the floor lane recoiled on alternate
    // landings instead of every one (the original springs it each bounce).
    case 0x647e: emit_sfx(kSfxHeldBump[d_79b9 & 0x2f]); f_654e(); break;
    case 0x654e: f_654e(); break;
    case 0x6587: f_6587(); break;
    case 0x6627: f_6627(); break;
    // Step-0 bump entries -- start the peg/block spring animations (and clear input
    // where the original does). Layer-B selects are no-ops while no block is bumped.
    case 0x6648: f_6648(); break;                                    // idle/rest peg spring
    case 0x6699: f_6699(); break;                                    // hop up-left  block
    case 0x66d8: f_66d8(); break;                                    // hop up-right block
    case 0x6748: f_6748(); break;                                    // up-left  peg(0x18)+block
    case 0x6789: f_6789(); break;                                    // up-right peg(0x19)+block
    case 0x67e2: case 0x6832: f_686a(kBumpSelL2); break;
    case 0x6813: case 0x684b: d_8570 = ball_.cell; f_686a(kBumpSelR2); break;
    case 0x67ca: if (col != 0) f_686a(kBumpSelL2); break;
    case 0x67fb: if (col != 7) { d_8570 = ball_.cell; f_686a(kBumpSelR2); } break;
    case 0x68fe: f_686a(kBumpSelL3); break;
    case 0x693a: f_686a(kBumpSelR3); break;
    case 0x68e6: if (col != 0) f_686a(kBumpSelL3); break;
    case 0x6922: if (col != 7) f_686a(kBumpSelR3); break;
    case 0x6890: f_6890(); break;
    case 0x68bb: f_68bb(); break;
    case 0x6326: f_6326(); break;  // roll-left  spike-death check (plane-B 0x0c)
    case 0x6372: f_6372(); break;  // roll-right spike-death check (plane-B 0x0c)
    case 0x640c: f_640c(); break;  // block bump: sfx + the picture-block match puzzle
    // Anim-sfx-only step handlers (PROJECT_STATUS.md: "anim steps 6305/64c1/645d are
    // sound-only"): no other gameplay effect, just the recovered speaker SFX id.
    case 0x6305: emit_sfx(0x03); break;
    case 0x645d: emit_sfx(0x0b); break;
    case 0x64c1: emit_sfx(0x0e); break;
    default: break;  // 0x7111 filler + 0x673a (confirmed empty)
    }
}

// ---- decide handlers ---------------------------------------------------------

void LevelGame::f_28f9() {
    d_824c = 8;
    if (d_79b4 == 0 && d_7924 != 0) {
        // FUN_1000_28f8 @ 0x292d: dropping onto the opened exit pit (tile 0x20) plays the
        // launch whoosh -- the SAME SFX 0x03 as the world-map cloud-jump -- before the
        // tile dispatch falls through (0x20 is neither 0x16 nor 0x03, so it lands on the
        // f_2965 default that arms the state-0x30 descent).
        if (d_7924 == 0x20) {
            emit_sfx(0x03);  // exit-pit fall
        }
        if (d_7924 == 0x16) {
            f_4305();  // nest tile: park the ball in it (state 0x1c) and spin
        } else if (d_7924 == 0x03) {
            // FUN_1000_463d settle: a 3-frame delay (using the sub-step lock as the
            // counter, which also inhibits motion meanwhile) then re-decide. The settle
            // plays once, on the frame the delay completes (not once per frame).
            if (++ball_.substep_lock == 3) {
                ball_.substep_lock = 0;
                emit_sfx(0x03);
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
            emit_sfx(0x14);  // climb a picture block
            f_69aa(0x24);  // spring the 0x0e tile above before climbing into it
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
    f_69aa(0x27);  // spring the hole as the ball drops in
    emit_sfx(0x03);
    f_4263(0x0e);  // -> warp state
    f_238e();
}

void LevelGame::f_2634() {  // hop up-left
    d_8551 = 0;
    // FUN_1000_2634 opens with an unconditional FUN_1000_63be() call: the roll/hop-bump
    // sfx, kSfxRollBump keyed by the tile under the ball (DAT_7924), gated on the prev
    // state not already rolling (DAT_8552 != 0x03/0x0f). (all_functions.c:3230-3231,
    // 7342-7363.)
    if (d_8552 != 0x03 && d_8552 != 0x0f) {
        emit_sfx(kSfxRollBump[d_7924 & 0x2f]);
    }
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
    // FUN_1000_26a1 opens with the same unconditional FUN_1000_63be() call as 2634.
    // (all_functions.c:3272-3273, 7342-7363.)
    if (d_8552 != 0x03 && d_8552 != 0x0f) {
        emit_sfx(kSfxRollBump[d_7924 & 0x2f]);
    }
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
    // FUN_1000_2810's fall-routing sfx: kSfxFallRoute keyed by the held-bump latched tile
    // (DAT_7922, NOT the fall lane), fired BEFORE the cell<8 split and gated on the prev
    // state not already falling/rolling (DAT_8552 != 0x03/0x0d/0x10). (all_functions.c:
    // 3433-3443.)
    if (d_8552 != 0x03 && d_8552 != 0x0d && d_8552 != 0x10) {
        emit_sfx(kSfxFallRoute[d_7922 & 0x2f]);
    }
    if (ball_.cell < 8) {
        f_4263(6);
    } else {
        d_856f = static_cast<std::uint8_t>(ball_.cell - 8);
        d_79b9 = grid_[d_856f];
        f_4263(d_79b9 < 0x30 ? kFallRoute[d_79b9 * 2] : 6);
        // The constant 0x14 fires only when the fall routed into state 0x0a (a picture-
        // block climb: kFallRoute[0x0e*2] == 0x0a). f_4263 set ball_.state, so this reads
        // the just-armed state -- DAT_792c == 0x0a after 4263. (all_functions.c:3455-3462.)
        if (ball_.state == 0x0a) {
            emit_sfx(0x14);
        }
        if (d_79b9 < 0x30 && kFallRoute[d_79b9 * 2 + 1] != 0) {
            f_69aa(kFallRoute[d_79b9 * 2 + 1]);  // spring the tile fallen onto
        }
    }
}

void LevelGame::f_23b6() {  // rolling
    if ((d_8244 & 0x04) == 0) {
        if ((d_8244 & 0x08) == 0) {
            const bool above_not_0e = ball_.cell < 8 || grid_[ball_.cell - 8] != 0x0e;
            if (above_not_0e && (d_8244 & 0x02)) {
                emit_sfx(0x14);  // rolling DOWN bump
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
    emit_sfx(0x14);  // chute/deadly-pit step
    if (d_7924 == 0x0f) {
        f_4802();
    } else if (d_7924 == 0x12 || d_7924 == 0x1f) {
        f_22b0();
    }
    d_8244 &= 0x1d;
    if (++d_824c == 9) {
        d_856f = static_cast<std::uint8_t>(ball_.cell - 8);
        f_69aa(0x24);  // spring the tile above at the top of the chute climb
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
    emit_sfx(0x03);
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
            f_69aa(0x27);  // spring the hole the ball pops out of
            emit_sfx(0x03);
            f_4263(0x0f);
            f_238e();
            return;
        }
    }
}

void LevelGame::f_1e3d() {
    // FUN_1000_1e3d: the terminal decision of the exit-portal descent (state 0x30).
    // The ball has finished sinking into the pit, so the board is cleared. The
    // original also marks the world-map node done (*9baa = 1) and bumps the cleared
    // count (a1a9, FUN_1000_3a88) -- that bookkeeping lives outside the board model.
    d_9d30 = 1;
}

void LevelGame::f_22b0() { f_22fc(); }

void LevelGame::f_22d2() {
    // FUN_1000_22d2: the death-tumble cascade. State 0x2e's decide slot. Each time the
    // 26-step fly-around script finishes, bump the loop counter; after the 3rd it ends
    // the life (22fc), otherwise it replays the tumble (the ball loops the screen ~3x).
    ++d_a0ce;
    if (d_a0ce == 3) {
        f_22fc();
    } else {
        f_4263(0x2e);
        f_238e();
    }
}

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
    emit_sfx(0x02);
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
    emit_sfx(0x02);
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

void LevelGame::f_228d() {  // entity hit -> death (shared with the spike-death cascade)
    d_a0ce = 1;
    ball_.step_index = 0;
    d_a1aa = 0;
    f_4263(0x2e);
}

// ===== Nest + block-top riding (worlds 2+) =====================================
// Recovered from FUN_1000_4305/4361/495c/4995 (the nest, tile 0x16) and
// FUN_1000_1e5e/1e90/1ec2/1f3e/1f03/1f7f/2138/21e7/21bb/2261 (riding the plane-B
// blocks: land on one from a hop -> sit on a cushion (0x0d) or walk along slabs
// (0x08), FIRE/DOWN smashing down through). The 6e11 calls these make are sound
// only (FUN_1000_6e30 is the sfx synth); wired via emit_sfx (1e5e/1e90 land = 0x0f,
// 2138/21e7 smash-down-through = 0x15 -- task 9, "wire in-game SFX triggers").

void LevelGame::f_495c(std::uint8_t wrap, std::uint8_t period, const std::int16_t* frames) {
    // FUN_1000_495c: every `period` frames advance the a0dc cycle and show that
    // ball frame (FUN_1000_4995). Drives the nest spin and the cushion bob.
    ++d_855d;
    if (d_855d == period) {
        f_4995(wrap, frames);
    } else if (d_855d > period) {
        d_855d = 0;
    }
}

void LevelGame::f_4995(std::uint8_t wrap, const std::int16_t* frames) {
    d_855d = 0;
    if (d_a0dc + 1 < wrap) {
        ++d_a0dc;
    } else {
        d_a0dc = 0;
    }
    ball_.frame = frames[d_a0dc];
}

void LevelGame::f_4361() { f_495c(0x15, 4, kNestSpinFrames); }

void LevelGame::f_4305() {
    ball_.state = 0x1c;  // in the nest (scriptless); FUN_1000_4344 decides the exit
    f_4361();
}

void LevelGame::f_1e5e() {
    // State 0x21: just landed from a hop up-left onto a block (d_8551 still holds
    // the target's plane-B value from FUN_1000_2634). A slab (0x08) chains straight
    // into the block-top walk (and thumps: 6e11(0xf)); the cushion (else) arm sits on
    // it silently. The 6e11 fires ONLY in the slab arm. (all_functions.c:2302-2314.)
    if (d_8551 == 0x08) {
        emit_sfx(0x0f);
        f_21e7();
    } else {
        ball_.state = 0x24;
    }
}

void LevelGame::f_1e90() {
    // Mirror of f_1e5e: the 6e11(0xf) fires only in the slab arm; the cushion (else)
    // arm (state 0x23) is silent. (all_functions.c:2337-2349.)
    if (d_8551 == 0x08) {
        emit_sfx(0x0f);
        f_2138();
    } else {
        ball_.state = 0x23;
    }
}

void LevelGame::f_1ec2() {
    // State 0x23: sitting on a cushion block -- bob through the DS:0x1ca4 frames;
    // DOWN rolls off.
    f_495c(0x0b, 5, kCushionBobL);
    if (d_8244 & 0x02) {
        f_1f03();
    }
}

void LevelGame::f_1f3e() {
    f_495c(0x0b, 5, kCushionBobR);
    if (d_8244 & 0x02) {
        f_1f7f();
    }
}

void LevelGame::f_1f03() {
    // Roll off the cushion leftward: reuse roll state 1 with the raw DS:0x140c
    // script (facing 9 negates dx; step_index 9 so the roll row's pickup steps
    // run), springing the block underneath (layer-B event 0x16).
    ball_.state = 0x01;
    ball_.script = &kCushionRollL;
    ball_.steps_left = kCushionRollL.count;
    ball_.facing = 9;
    ball_.step_index = 9;
    d_8570 = ball_.cell;
    f_6a89(0x16);
    f_238e();
}

void LevelGame::f_1f7f() {
    ball_.state = 0x02;
    ball_.script = &kCushionRollR;
    ball_.steps_left = kCushionRollR.count;
    ball_.facing = 0;
    ball_.step_index = 9;
    d_8570 = static_cast<std::uint8_t>(ball_.cell - 1);
    f_6a89(0x16);
    f_238e();
}

void LevelGame::f_2138() {
    // State 0x25: walking left along block tops. FIRE/DOWN smashes down through
    // (state 0x32); at column 0 hop off the edge (state 0x27); otherwise the next
    // state comes from DS:0x42d6 by the left neighbour's plane-B value, the 0x25
    // sentinel re-checking plane A (FUN_1000_21bb).
    d_8551 = 0;
    if ((d_8244 & 0x12) == 0) {
        if (ball_.cell_col == 0) {
            d_8551 = 0x1f;
            f_4263(0x27);
        } else {
            d_8570 = static_cast<std::uint8_t>(ball_.cell - 1);
            f_6bd4(d_8570);
            const std::uint8_t code = k42d6[d_8551 & 0x1f];
            if (code == 0x25) {
                f_21bb();
            } else {
                f_4263(code);
            }
        }
    } else {
        emit_sfx(0x15);  // smash down through the block
        f_4263(0x32);
    }
    f_238e();
}

void LevelGame::f_21e7() {
    // State 0x26: walking right along block tops; symmetric to f_2138 but keyed by
    // the CURRENT cell's plane-B via DS:0x42f6 (and no d_8551 reset -- original).
    if ((d_8244 & 0x12) == 0) {
        if (ball_.cell_col == 7) {
            d_8551 = 0x1f;
            f_4263(0x28);
        } else {
            d_8570 = ball_.cell;
            f_6bd4(ball_.cell);
            const std::uint8_t code = k42f6[d_8551 & 0x1f];
            if (code == 0x26) {
                f_2261();
            } else {
                f_4263(code);
            }
        }
    } else {
        emit_sfx(0x15);  // smash down through the block
        f_4263(0x33);
    }
    f_238e();
}

void LevelGame::f_21bb() {
    f_6bb5(static_cast<std::uint8_t>(ball_.cell - 1));
    f_4263(d_7921 == 0x0b ? 0x29 : 0x25);
}

void LevelGame::f_2261() {
    f_6bb5(static_cast<std::uint8_t>(ball_.cell + 1));
    f_4263(d_7921 == 0x0b ? 0x2a : 0x26);
}

// ===== Picture-block match puzzle (plane-B 0x0e..0x11) =========================
// Bumping a picture block cycles its art (0x0e -> 0x0f -> 0x10 -> 0x11 -> 0x0e,
// via the ordinary layer-B bump events); FUN_1000_6183 (recovered from raw bytes,
// Ghidra failed on it) checks after each such bump whether every remaining
// picture block shows the SAME picture, and if so lists every plane-B 0x05 block
// in the DS:0x886 buffer. FUN_1000_629c (main loop) then pops them open one at a
// time -- layer-B event 0x18 clears the tile -- every 11 frames.

void LevelGame::f_640c() {
    // Block-bump anim step: plays the per-block sound (kSfxPictureBlock, keyed by the
    // pre-bump plane-B value DAT_8551), then, if that value was a picture (0x0e..0x11),
    // re-checks the match puzzle.
    emit_sfx(kSfxPictureBlock[d_8551 & 0x1f]);
    if (d_8551 > 0x0d && d_8551 < 0x12) {
        f_6183();
    }
}

void LevelGame::f_6183() {
    // Any check first rewinds/clears the list (so re-bumping mid-cascade stops it).
    cascade_cursor_ = 0;
    d_0886_[0] = 0xff;
    std::uint8_t found = 0;
    for (int c = 0; c < 0x30 && found == 0; ++c) {
        const std::uint8_t v = grid_[c + 0x30];
        if (v >= 0x0e && v < 0x12) {
            found = v;
        }
    }
    if (found == 0) {
        return;
    }
    bool mixed = false;
    for (int c = 0; c < 0x30 && !mixed; ++c) {
        const std::uint8_t v = grid_[c + 0x30];
        if (v >= 0x0e && v < 0x12 && v != found) {
            mixed = true;
        }
    }
    if (mixed) {
        return;
    }
    std::uint8_t count = 0;
    for (int c = 0; c < 0x30; ++c) {
        if (grid_[c + 0x30] == 0x05) {
            d_0886_[count++] = static_cast<std::uint8_t>(c);
        }
    }
    if (d_0886_[count] == 0xff) {
        return;  // original quirk: a stale terminator at this slot skips the re-arm
    }
    d_0886_[count] = 0xff;
    cascade_cursor_ = 0;
    d_79b7 = 0;
}

void LevelGame::f_629c() {
    if (d_0886_[cascade_cursor_] == 0xff) {
        cascade_cursor_ = 0;
        d_0886_[0] = 0xff;
        return;
    }
    if (d_79b7 == 0) {
        d_79b7 = 0x0a;
        d_8570 = d_0886_[cascade_cursor_];
        f_6a89(0x18);  // pop the block open (new_tile 0x00); kSfxLayerBBlock[0x18] == 0, silent
        ++cascade_cursor_;
    } else {
        --d_79b7;
    }
}

// ===== Moving entity (monster) =================================================
// Recovered from FUN_1000_2a78/48a9/4bc6/4c14/4c99/5003/5085/50c0/50fb and the
// AI routines 4dbf/4e44/4ec9/4f4e + leaves 4dfa/4e7f/4f04/4f89. See
// analysis/specs/game-loop.md ("Moving entity"). The entity walks the plane-A/B
// maze a cell at a time, keeping its heading when free and turning by a fixed
// preference otherwise; ball contact arms the shared state-0x2e death cascade.

void LevelGame::f_48a9() {
    // FUN_1000_48a9: derive col/row + pixel position from the entity cell. The
    // pixel anchor is bum_cell_position (DS:0x274) + (7,7), centred like the ball.
    if (d_8571 == 0xff) {
        return;
    }
    d_8565 = static_cast<std::uint8_t>(d_8571 >> 3);          // row
    d_8564 = static_cast<std::uint8_t>(d_8571 - d_8565 * 8);  // col
    const CellPosition p = bum_cell_position(d_8564, d_8565);
    d_79ba = p.x + 7;
    d_79bc = p.y + 7;
}

void LevelGame::f_4bc6(std::uint8_t behaviour) {
    // FUN_1000_4bc6: load movement script `behaviour` (DS:0x2520). The keyframe
    // cursor is implicit -- kf index = script.kf + (script.count - a1b0).
    d_8562 = behaviour;
    const EntityScript& s = kEntityScripts[behaviour < 10 ? behaviour : 0];
    d_a1b0 = s.count;
    d_9d2f = s.dir;
}

void LevelGame::f_4c14() {
    // FUN_1000_4c14: step the movement script on every other frame (8243 toggle).
    d_8243 ^= 1;
    if (d_8243 == 0 || d_8571 == 0xff) {
        return;
    }
    const EntityScript& s = kEntityScripts[d_8562 < 10 ? d_8562 : 0];
    if (s.count == 0 || d_a1b0 == 0) {
        return;  // null script, or exhausted (4c99 reloads it the same frame)
    }
    const std::size_t idx = s.kf + (s.count - d_a1b0);
    const EntityKeyframe& k = kEntityKeyframes[idx];
    d_8560 = static_cast<std::uint8_t>(k.frame);
    d_79ba += (d_9d2f != 0) ? -k.dx : k.dx;
    d_79bc += k.dy;
    --d_a1b0;
    if (d_a1b0 == 0) {
        d_8563 = 0;
    } else {
        ++d_8563;
    }
}

void LevelGame::f_4c99() {
    // FUN_1000_4c99: only on active frames. On arrival (a1b0==0) compute the four
    // free-flags over the live grid and dispatch the AI; otherwise advance the cell.
    if (d_8243 == 0 || d_8571 == 0xff) {
        return;
    }
    if (d_a1b0 != 0) {
        f_5003();
        return;
    }
    const std::uint8_t cell = d_8571;
    d_a1b2 = d_a0e2 = d_a0e1 = d_a0e0 = 1;            // default: blocked
    if (cell > 7 && grid_[cell - 8] == 0) {
        d_a0e0 = 0;                                    // UP free
    }
    if (cell < 0x28 && grid_[cell] == 0) {
        d_a0e1 = 0;                                    // DOWN free (reads its own cell -- faithful)
    }
    if (d_8564 != 0 && grid_[cell + 0x2f] == 0) {      // LEFT: plane-B of cell-1
        d_a0e2 = 0;
        if (grid_[cell - 1] == 0x0b) {
            d_a0e2 = 1;                                // a plane-A 0x0b wall re-blocks
        }
    }
    if (d_8564 != 7 && grid_[cell + 0x31] == 0) {      // RIGHT: plane-B of cell+1
        d_a1b2 = 0;
        if (grid_[cell + 1] == 0x0b) {
            d_a1b2 = 1;
        }
    }
    if (d_a0e0 + d_a0e1 + d_a0e2 + d_a1b2 == 4) {
        f_4fd3();                                      // boxed in -> random bob
    } else {
        entity_ai_arrive(d_8562);
    }
}

void LevelGame::f_5003() {
    // FUN_1000_5003: at the visual mid-step (8563==5) flip the cell index.
    if (d_8563 == 5) {
        entity_ai_mid(d_8562);
    }
}

void LevelGame::f_4fd3() {
    // FUN_1000_4fd3: all four directions blocked -> pick a random bob script 5..8.
    const std::uint8_t base = d_79b3 & 3;
    const std::uint16_t r = prng_next();  // FUN_1000_93b1
    f_4bc6(static_cast<std::uint8_t>((r & 1) + base + 5));
}

void LevelGame::entity_ai_arrive(std::uint8_t behaviour) {
    switch (behaviour) {  // DS:0x870
        case 1: f_4dbf(); break;
        case 2: f_4e44(); break;
        case 3: f_4ec9(); break;
        case 4: f_4f4e(); break;
        case 5: case 6: case 7: case 8: case 9: f_4dbf(); break;
        default: break;  // 0 / 10 -> 7111 (noop)
    }
}

void LevelGame::entity_ai_mid(std::uint8_t behaviour) {
    switch (behaviour) {  // DS:0x85c
        case 1: f_5025(); break;
        case 2: f_503f(); break;
        case 3: f_5059(); break;
        case 4: f_506f(); break;
        default: break;  // noop
    }
}

void LevelGame::f_4dbf() {  // was UP: prefer up, then right, left, down
    if (d_a0e0 == 0) f_4dfa();
    else if (d_a1b2 == 0) f_4f89();
    else if (d_a0e2 == 0) f_4f04();
    else f_4e7f();
}

void LevelGame::f_4e44() {  // was DOWN: prefer down, then left, right, up
    if (d_a0e1 == 0) f_4e7f();
    else if (d_a0e2 == 0) f_4f04();
    else if (d_a1b2 == 0) f_4f89();
    else f_4dfa();
}

void LevelGame::f_4ec9() {  // was LEFT: prefer left, then up, down, right
    if (d_a0e2 == 0) f_4f04();
    else if (d_a0e0 == 0) f_4dfa();
    else if (d_a0e1 == 0) f_4e7f();
    else f_4f89();
}

void LevelGame::f_4f4e() {  // was RIGHT: prefer right, then down, up, left
    if (d_a1b2 == 0) f_4f89();
    else if (d_a0e1 == 0) f_4e7f();
    else if (d_a0e0 == 0) f_4dfa();
    else f_4f04();
}

void LevelGame::f_4dfa() {  // commit UP (1); random detour only when 79b3 < 7920
    std::uint8_t next = 1;
    if (d_79b3 < d_7920) {
        if ((d_79b3 & 1) == 0) {
            next = (d_a1b2 == 0) ? 4 : 1;
        } else if (d_a0e2 == 0) {
            f_4bc6(3);
            return;
        }
    }
    f_4bc6(next);
}

void LevelGame::f_4e7f() {  // commit DOWN (2)
    std::uint8_t next = 2;
    if (d_79b3 < d_7920) {
        if ((d_79b3 & 1) == 0) {
            next = (d_a1b2 == 0) ? 4 : 2;
        } else if (d_a0e2 == 0) {
            f_4bc6(3);
            return;
        }
    }
    f_4bc6(next);
}

void LevelGame::f_4f04() {  // commit LEFT (3)
    std::uint8_t next = 3;
    if (d_79b3 < d_7920) {
        if ((d_79b3 & 1) == 0) {
            next = (d_a0e1 == 0) ? 2 : 3;
        } else if (d_a0e0 == 0) {
            f_4bc6(1);
            return;
        }
    }
    f_4bc6(next);
}

void LevelGame::f_4f89() {  // commit RIGHT (4)
    std::uint8_t next = 4;
    if (d_79b3 < d_7920) {
        if ((d_79b3 & 1) == 0) {
            next = (d_a0e1 == 0) ? 2 : 4;
        } else if (d_a0e0 == 0) {
            f_4bc6(1);
            return;
        }
    }
    f_4bc6(next);
}

void LevelGame::f_5025() { d_8571 -= 8; --d_8565; }  // cell up
void LevelGame::f_503f() { d_8571 += 8; ++d_8565; }  // cell down
void LevelGame::f_5059() { d_8571 -= 1; --d_8564; }  // cell left
void LevelGame::f_506f() { d_8571 += 1; ++d_8564; }  // cell right

void LevelGame::f_5085() {
    // FUN_1000_5085: ball AABB box around (9290,9292), frozen during death (a0ce).
    if (d_a0ce != 0) {
        return;
    }
    box_ball_x0_ = ball_.x - 5;
    box_ball_x1_ = ball_.x + 6;
    box_ball_y0_ = ball_.y - 5;
    box_ball_y1_ = ball_.y + 5;
}

void LevelGame::f_50c0() {
    // FUN_1000_50c0: entity AABB box around (79ba,79bc).
    if (d_a0ce != 0) {
        return;
    }
    box_ent_x0_ = d_79ba - 5;
    box_ent_x1_ = d_79ba + 6;
    box_ent_y0_ = d_79bc - 5;
    box_ent_y1_ = d_79bc + 5;
}

void LevelGame::f_50fb() {
    // FUN_1000_50fb: standard box overlap. On contact set a1aa (consumed by 1d26
    // next frame -> f_228d) and emit the 6e11 death sfx (id 0x03). Skipped while the
    // ball is descending the exit pit (state 0x30) or already dying (a0ce/856d).
    if (d_8571 == 0xff || d_a0ce != 0 || d_856d != 0 || ball_.state == 0x30) {
        return;
    }
    if (box_ball_x1_ < box_ent_x0_ || box_ent_x1_ < box_ball_x0_ ||
        box_ball_y1_ < box_ent_y0_ || box_ent_y1_ < box_ball_y0_) {
        d_a1aa = 0;
    } else {
        d_a1aa = 1;
        emit_sfx(0x03);  // monster death
    }
}

void LevelGame::f_6326() {
    // FUN_1000_6326: roll-left step-4 spike check. If the cell to the left (cell-1)
    // holds a plane-B vertical spike (0x0c), the ball dies with the fly-around tumble
    // (state 0x2e) -- same arming as the entity-hit f_228d, minus the a1aa clear.
    if (ball_.cell_col != 0 && grid_[ball_.cell + 0x2f] == 0x0c) {
        ball_.step_index = 0;
        d_a0ce = 1;
        emit_sfx(0x03);  // spike death
        f_4263(0x2e);
    }
}

void LevelGame::f_6372() {
    // FUN_1000_6372: roll-right step-4 spike check. A plane-B vertical spike (0x0c) on
    // the ball's own cell arms the fly-around death tumble (state 0x2e).
    if (ball_.cell_col != 7 && grid_[ball_.cell + 0x30] == 0x0c) {
        ball_.step_index = 0;
        d_a0ce = 1;
        emit_sfx(0x03);  // spike death
        f_4263(0x2e);
    }
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
        // FUN_1000_6d94: arm the layer-A spring (FUN_1000_69aa) at the ball's cell.
        // This is what makes the structure under the ball recoil -- e.g. the
        // special bumpers 0x14/0x15 (events 0x2d/0x2e) on world-1 node 14, which
        // throw the ball but, until this call was wired, never sprang back.
        f_6d94(trigger);
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
        f_69aa(action);  // spring the bumped peg (same id drives the forced fall)
    }
}

void LevelGame::f_6587() {
    if (d_a1a7 == 0 && d_7924 == 0x02 && (d_8244 & 0x02)) {
        d_856f = ball_.cell;
        d_79b4 = 0x34;
        emit_sfx(0x04);
        f_69aa(0x34);  // the 0x02-lane DOWN auto-roll spring
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
    // Both 6e11 emits live INSIDE this exclusion guard, so the free bonus tiles ('#'
    // extra-life 0x23 and the 0x01 lane) collect silently -- only tiles that count toward
    // the exit make a sound. (all_functions.c:8285-8309.)
    if (d_79b8 != 0x01 && d_79b8 != 0x23) {
        --d_a0cf;
        if (d_a0cf == 0) {
            // Last required collectible taken: OPEN the exit portal. FUN_1000_69aa(0x59)
            // writes the pit tile 0x20 into plane A at the portal cell (DAT_8572) and
            // plays the one-shot opening animation. The board is NOT cleared yet -- the
            // ball must roll to the pit and fall in (tile 0x20 -> state 0x30 -> 1e3d).
            // a1b1/8550 then drive the recurring pulse (FUN_1000_233a).
            emit_sfx(0x0b);  // portal-open jingle
            d_856f = d_8572;
            f_69aa(0x59);
            d_a1b1 = 1;
            d_8550 = 0xf2;
        } else {
            emit_sfx(0x0e);  // ordinary required-collectible pickup
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

// ---- tile bump/spring animations ---------------------------------------------
// FUN_1000_14e4 / 15a1 step the active slots; FUN_1000_69aa / 6a89 arm one. A slot
// plays a sprite-index byte stream (0x00 = hold, 0xff = end); each index resolves
// through the per-layer record table to a {frame, y_offset}. See object_anim.h.

void LevelGame::anim_step(AnimSlot& s, const AnimRecord* recs, std::size_t rec_count) {
    if (!s.active) {
        return;
    }
    const std::uint8_t b = s.stream[s.cursor++];
    if (b == 0xff) {
        s.active = false;  // terminator: free the slot
        return;
    }
    if (b != 0 && b < rec_count) {  // b == 0 holds the previous step's sprite
        s.frame_index = recs[b].frame_index;
        s.y_offset = recs[b].y_offset;
    }
}

void LevelGame::f_14e4() {
    for (auto& s : anim_a_) {
        anim_step(s, kAnimRecordA, kAnimRecordACount);
    }
}

void LevelGame::f_15a1() {
    for (auto& s : anim_b_) {
        anim_step(s, kAnimRecordB, kAnimRecordBCount);
    }
}

void LevelGame::anim_arm(AnimSlot* slots, std::size_t n, std::uint8_t cell, const BumpEvent& ev,
                         const std::uint8_t* pool, std::size_t plane_off, const AnimRecord* recs,
                         std::size_t rec_count) {
    // Re-trigger the same cell's slot if one is live, else take a free slot; if all
    // are busy the original simply drops the request.
    AnimSlot* dst = nullptr;
    for (auto* end = slots + n, *p = slots; p != end; ++p) {
        if (p->active && p->cell == cell) {
            dst = p;
            break;
        }
    }
    if (dst == nullptr) {
        for (auto* end = slots + n, *p = slots; p != end; ++p) {
            if (!p->active) {
                dst = p;
                break;
            }
        }
    }
    if (dst == nullptr) {
        return;
    }
    grid_[cell + plane_off] = ev.new_tile;  // swap to the settled "pressed" tile
    dst->active = true;
    dst->cell = cell;
    dst->stream = pool + ev.stream_offset;
    dst->cursor = 0;
    dst->frame_index = kAnimHiddenFrame;
    dst->y_offset = 0;
    anim_step(*dst, recs, rec_count);  // show step 0 this frame (see tick() ordering)
}

void LevelGame::f_69aa(std::uint8_t id) {
    if (id == 0 || id >= kBumpEventACount || kBumpEventA[id].stream_len == 0) {
        return;
    }
    anim_arm(anim_a_.data(), anim_a_.size(), d_856f, kBumpEventA[id], kBumpEventAStream,
             0x00, kAnimRecordA, kAnimRecordACount);
}

void LevelGame::f_6a89(std::uint8_t id) {
    emit_sfx(kSfxLayerBBlock[id & 0x1f]);  // layer-B block, keyed by the event id itself
    if (id == 0 || id >= kBumpEventBCount || kBumpEventB[id].stream_len == 0) {
        return;
    }
    anim_arm(anim_b_.data(), anim_b_.size(), d_8570, kBumpEventB[id], kBumpEventBStream,
             0x30, kAnimRecordB, kAnimRecordBCount);
}

void LevelGame::f_6987(std::uint8_t id) {
    d_856f = ball_.cell;
    if (id != 0) {
        f_69aa(id);
    }
}

void LevelGame::f_6d94(std::uint8_t id) {
    d_856f = ball_.cell;
    f_69aa(id);
}

void LevelGame::f_6d6a(const std::uint8_t* tile_map) {
    // FUN_1000_6d6a: while not sub-step-locked, spring the lane under the ball,
    // keyed by the tile there. This is the platform recoil when a roll begins.
    if (ball_.substep_lock == 0) {
        f_6987(d_7924 < 0x30 ? tile_map[d_7924] : 0);
    }
}

void LevelGame::f_686a(std::uint8_t row) {
    f_6a89(kBumpSelectB[row][d_8551 & 0x1f]);
    d_8244 = 0;
}

void LevelGame::f_6648() {
    emit_sfx(kSfxIdleRest[d_7924 & 0x2f]);  // idle-rest, keyed by the tile under the ball
    f_6987(d_7924 < 0x30 ? kIdleSpringA[d_7924] : 0);
}

void LevelGame::f_6699() {
    if (d_8552 != 0x03 && d_8552 != 0x0f) {  // not already rolling -> recoil the lane
        f_6d6a(kRollSpringL);
    }
    if (ball_.cell_col != 0) {
        f_6a89(kBumpSelectB[kBumpSelL0][d_8551 & 0x1f]);
    }
}

void LevelGame::f_66d8() {
    if (d_8552 != 0x03 && d_8552 != 0x0f) {
        f_6d6a(kRollSpringR);
    }
    if (ball_.cell_col != 7) {
        f_6a89(kBumpSelectB[kBumpSelR0][d_8551 & 0x1f]);
    }
}

void LevelGame::f_6748() {
    emit_sfx(0x08);  // hop-up entry
    f_6d94(0x18);
    if (ball_.cell_col != 0) {
        f_6a89(kBumpSelectB[kBumpSelL1][d_8551 & 0x1f]);
    }
}

void LevelGame::f_6789() {
    emit_sfx(0x08);  // hop-up entry
    f_6d94(0x19);
    if (ball_.cell_col != 7) {
        f_6a89(kBumpSelectB[kBumpSelR1][d_8551 & 0x1f]);
    }
}

void LevelGame::f_6890() {
    if (ball_.cell_col != 0) {
        f_6a89(kBumpSelectB[kBumpSelL1][d_8551 & 0x1f]);
        d_8244 = 0;
    }
}

void LevelGame::f_68bb() {
    if (ball_.cell_col != 7) {
        f_6a89(kBumpSelectB[kBumpSelR1][d_8551 & 0x1f]);
        d_8244 = 0;
    }
}

std::size_t LevelGame::object_anims(std::array<ObjectAnimSprite, 7>& out) const {
    std::size_t n = 0;
    for (const auto& s : anim_a_) {
        if (s.active) {
            out[n++] = ObjectAnimSprite{s.cell, s.frame_index, s.y_offset, false};
        }
    }
    for (const auto& s : anim_b_) {
        if (s.active) {
            out[n++] = ObjectAnimSprite{s.cell, s.frame_index, s.y_offset, true};
        }
    }
    return n;
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
        // FUN_1000_431b: fire in the nest -- hop out on a direction, else keep spinning.
        if (d_8244 & 0x04) {
            f_2634();
        } else if (d_8244 & 0x08) {
            f_26a1();
        } else {
            f_4361();
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
        f_4305();  // already a nest: park in it
    } else {
        // Dig a fresh nest: event 0x2f writes tile 0x16 under the ball (with a
        // 2-frame dig anim); the next decide then parks in it via the branch above.
        f_6d94(0x2f);
    }
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
    // Hopping up-left out of the cloud chain from a NON-cloud tile first digs a
    // cloud at the departure cell (event 0x2f), so the bonk/return has a perch.
    if (d_7924 != 0x16) {
        f_6d94(0x2f);
    }
    f_2634();
}

void LevelGame::f_457a() {
    if (d_7924 != 0x16) {
        f_6d94(0x2f);
    }
    f_26a1();
}

void LevelGame::f_45a0(std::uint8_t state) {
    // Commit a cloud-chain move. Event 0x30 (new_tile 0x00, the 2-frame dissolve)
    // ERASES the tile being left -- this is what makes the ridden cloud (tile
    // 0x16) move with the ball instead of duplicating: erased here, re-dug by
    // 440c/450c/457a at the destination. The port used to drop this call.
    f_4263(state);
    if (d_7924 != 0) {
        f_6d94(0x30);
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

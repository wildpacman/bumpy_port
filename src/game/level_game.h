#pragma once

#include "game/ball_motion.h"
#include "resources/level_resources.h"  // BumEntities

#include <array>
#include <cstdint>

namespace bumpy {

// One frame of player input (already debounced/sticky handling is internal). The
// fields are physical directions; build_input_bits() maps them to the original
// FUN_1000_75a2 action bits (up=0x01, down=0x02, left=0x04, right=0x08, fire=0x10).
struct LevelInput {
    bool left{};
    bool right{};
    bool up{};
    bool down{};
    bool fire{};
};

enum class LevelStatus {
    playing,
    won,   // all required collectibles taken (DAT_856d)
    dead,  // player died (DAT_9d30) -- only via an entity, so not on board 0
    quit,  // quit-to-menu (DAT_928d) -- F7 etc.
};

// The platform-independent in-level gameplay loop for one board: the player ball
// state machine (analysis/specs/game-loop.md) over a mutable 3-plane grid, plus
// collect/score/lives/win. One tick() == one 70 Hz frame. Faithfully transcribed
// from FUN_1000_0c18's playfield body and the FUN_1000_1d26 player tick; method
// names keep the original FUN_1000_* address. Sound and pure-cosmetic sprite work
// are no-ops. Entity collision/AI is out of scope (board 0 / world-1 node 1 has no
// entity).
class LevelGame {
public:
    // Start a board from its decoded 194-byte BUM record, carrying lives/score.
    explicit LevelGame(const BumEntities& board, std::uint8_t lives = 5,
                       std::uint32_t score = 0);

    // Advance one frame.
    void tick(const LevelInput& input);

    // --- Views for rendering / tests ---
    [[nodiscard]] const BallMotion& ball() const noexcept { return ball_; }
    [[nodiscard]] int ball_x() const noexcept { return ball_.x; }
    [[nodiscard]] int ball_y() const noexcept { return ball_.y; }
    [[nodiscard]] int ball_frame() const noexcept { return ball_.frame; }
    [[nodiscard]] std::uint8_t ball_cell() const noexcept { return ball_.cell; }
    [[nodiscard]] std::uint8_t player_state() const noexcept { return ball_.state; }

    [[nodiscard]] LevelStatus status() const noexcept { return status_; }
    [[nodiscard]] std::uint8_t lives() const noexcept { return d_791a; }
    [[nodiscard]] std::uint32_t score() const noexcept;
    [[nodiscard]] std::uint8_t collectibles_left() const noexcept { return d_a0cf; }

    // The live plane-C collectible value at a cell (0 once collected), for the
    // renderer to draw only what remains. cell = row*8 + col.
    [[nodiscard]] std::uint8_t collectible(int col, int row) const;
    // Whole live grid (a0d8: planes A/B/C + header) plus trailing slack, for the
    // renderer. Only [0, 0x96) is meaningful.
    [[nodiscard]] const std::array<std::uint8_t, 0x100>& grid() const noexcept { return grid_; }

private:
    // --- mirrored DS:0x* globals (kept named for 1:1 auditability) ---
    // 0x96 live bytes (3 planes A/B/C + header) padded to 0x100 so an out-of-range
    // cell index (e.g. a +0x60 plane-C read at a stray cell) can't read past the end.
    std::array<std::uint8_t, 0x100> grid_{};  // a0d8
    BallMotion ball_{};                      // 792c/824d/792a/9bae/8242/824a/9290/9292/856e/855c/855e

    std::uint8_t d_8244{};   // current action bits (sticky)
    std::uint8_t d_7924{};   // plane-A value under the ball
    std::uint8_t d_79b8{};   // plane-C value at a queried cell
    std::uint8_t d_79b9{};   // plane-A value at a queried cell (fall routing)
    std::uint8_t d_8551{};   // plane-B value at a queried cell
    std::uint8_t d_7921{};   // plane-A value at a queried cell (6bb5)
    std::uint8_t d_7920{};   // board sub-type (header 0x95)
    std::uint8_t d_7922{};   // held-bump latched tile
    std::uint8_t d_7923{};   // structure-trigger guard (blocks the held-bump latch)
    std::uint8_t d_8552{};   // previous player state
    std::uint8_t d_8570{};   // target cell
    std::uint8_t d_856f{};   // scratch cell
    std::uint8_t d_8571{};   // entity-2 cell (unused on board 0)
    std::uint8_t d_8572{};   // secondary start cell (header 0x91 - 1)
    std::uint8_t d_79b4{};   // auto-roll countdown (0x34 = trigger)
    std::uint8_t d_a1a7{};   // held-bump action (drives forced fall)
    std::uint8_t d_a0ce{};   // forced-fall inhibit
    std::uint8_t d_a1aa{};   // enemy-hit pending (always 0 on board 0)
    std::uint8_t d_a0cf{};   // required collectibles remaining
    std::uint8_t d_791a{};   // lives
    std::uint16_t d_a0d4{};  // score low word
    std::uint16_t d_a0d6{};  // score high word
    std::uint8_t d_a1b1{};   // win cascade flag
    std::uint8_t d_8550{};   // win timer
    std::uint8_t d_824c{};   // fall/landing counter
    std::uint8_t d_79b3{};   // PRNG output byte (used by 4747 idle-blink select)
    std::uint16_t prng_state_{0x2c9b};  // 16-bit LCG state behind FUN_1000_93b1

    std::uint8_t d_928d{};   // quit
    std::uint8_t d_856d{};   // win
    std::uint8_t d_9d30{};   // death
    LevelStatus status_{LevelStatus::playing};

    // --- core loop ---
    std::uint16_t prng_next();                  // FUN_1000_93b1
    std::uint8_t build_input_bits(const LevelInput&) const;  // FUN_1000_75a2 (port mapping)
    void f_1d26(const LevelInput&);             // player tick
    void f_1e02();                              // decide dispatch (0x7ca)
    void f_238e();                              // animate dispatch (0x43c0)
    void decide_dispatch(std::uint8_t state);
    void anim_dispatch(std::uint8_t state, std::uint8_t step);

    // --- helpers ---
    void f_236f();                              // d_7924 = grid_[cell]  (plane A)
    void f_1dde(const LevelInput&);             // d_8244 = built (if nonzero)
    void f_4263(std::uint8_t new_state);        // arm + consume input
    void f_4906();                              // set ball pixel from cell
    void f_6bb5(std::uint8_t cell);             // d_7921 = grid_[cell]
    void f_6bd4(std::uint8_t cell);             // d_8551 = grid_[cell + 0x30]
    void f_6bf4(std::uint8_t cell);             // d_79b8 = grid_[cell + 0x60]
    void f_6717();                              // structure-trigger at the current cell
    void f_6d26();                              // structure-trigger dispatch
    void f_654e();                              // held-bump latch
    void f_695e(std::uint8_t action);           // arm a held bump
    void f_6587();                              // 0x02-lane + RIGHT auto-roll
    void f_6627();                              // pickup collectible if present
    void f_6c14();                              // collect: clear cell, score, win check
    void f_6c95();                              // score the collectible

    // --- decide handlers (0x7ca) ---
    void f_28f9();  // idle hub
    void f_2965();
    void f_29a6();
    void f_465e();  // none/up/down reaction-table reads -> 46bb
    void f_467d();
    void f_469c();
    void f_46bb(std::uint8_t code);
    void f_472d(std::uint8_t code);
    void f_4747();  // roll / idle-blink
    void f_47cb();  // on 0x0a special lane
    void f_4802();  // hole -> warp
    void f_2634();  // hop up-left
    void f_26a1();  // hop up-right
    void f_270c();  // bumper
    void f_2776();  // bumper
    void f_27de();  // fall
    void f_2810();  // fall routing
    void f_23b6();  // rolling
    void f_2470();  // falling-begin
    void f_248e();  // falling/float
    void f_24d7();  // landing test
    void f_250a();  // roll-after-land
    void f_253f();  // chute step
    void f_28e0();  // -> state 4
    void f_42d9();  // -> state 0x2d
    void f_25ad();  // warp
    void f_22b0();  // clear -> win
    void f_22fc();  // win finish
    void f_2423();  // bounce (scriptless state 5)
    void f_1fbe();  // special bumper
    void f_207d();  // special bumper
    void f_228d();  // death (entity)

    // --- input-decode tree (0x43c0 chaining) ---
    void f_4437();
    void f_4344();
    void f_4398();
    void f_43b5();
    void f_43d2();
    void f_43ef();
    void f_440c();
    void f_4454();
    void f_448a();
    void f_44c0();
    void f_4532();
    void f_450c();
    void f_457a();
    void f_45a0(std::uint8_t state);
    [[nodiscard]] bool f_45cf(std::uint8_t cell);  // plane-A occupied (and != 0x19)
    [[nodiscard]] bool f_4605(std::uint8_t cell);  // plane-B occupied (and != 0x13)
};

}  // namespace bumpy

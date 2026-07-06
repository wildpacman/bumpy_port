#pragma once

#include "game/ball_motion.h"
#include "game/object_anim.h"  // AnimRecord, ObjectAnimSprite
#include "resources/level_resources.h"  // BumEntities

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bumpy {

// One frame of player input (already debounced/sticky handling is internal). The
// direction/fire fields map (build_input_bits) to the original FUN_1000_75a2 action
// bits (up=0x01, down=0x02, left=0x04, right=0x08, fire=0x10). `cancel` is the Escape
// key: FUN_1000_1d26 polls scancode 0x01 separately (it is NOT part of the 75a2 action
// mask) and calls FUN_1000_22fc -- lose a life -- so in-level Escape ends the board like
// a death (back to the world map, node unmarked; GAME OVER on the last life).
struct LevelInput {
    bool left{};
    bool right{};
    bool up{};
    bool down{};
    bool fire{};
    bool cancel{};
};

enum class LevelStatus {
    playing,
    won,   // board cleared: the ball fell into the exit portal (DAT_9d30, via FUN_1000_1e3d)
    dead,  // lost a life: enemy death / deadly pit / F2 skip (DAT_856d, via FUN_1000_22fc)
    quit,  // quit-to-menu (DAT_928d) -- F7 etc.
};

// The platform-independent in-level gameplay loop for one board: the player ball
// state machine (analysis/specs/game-loop.md) over a mutable 3-plane grid, plus
// collect/score/lives/win. One tick() == one 70 Hz frame. Faithfully transcribed
// from FUN_1000_0c18's playfield body and the FUN_1000_1d26 player tick; method
// names keep the original FUN_1000_* address. Every recovered FUN_1000_6e11(id)
// sound-trigger site pushes the speaker-profile SFX id to a queue (emit_sfx /
// take_sfx_events) instead of calling SDL directly, keeping this class SDL-free;
// the platform shell drains the queue into AudioEngine::play_sfx after each
// tick(). Pure-cosmetic sprite work is still a no-op. Entity collision/AI is out
// of scope (board 0 / world-1 node 1 has no entity).
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

    // The moving entity (monster), for rendering. Most boards have none. The frame
    // is the BUMSPJEU bank index (a0de + the current keyframe); draw it centred on
    // (monster_x, monster_y) like the ball. See analysis/specs/game-loop.md
    // ("Moving entity"). FUN_1000_1cea.
    [[nodiscard]] bool monster_present() const noexcept { return d_8571 != 0xff; }
    [[nodiscard]] int monster_x() const noexcept { return d_79ba; }
    [[nodiscard]] int monster_y() const noexcept { return d_79bc; }
    [[nodiscard]] int monster_frame() const noexcept { return d_a0de + d_8560; }
    [[nodiscard]] std::uint8_t monster_cell() const noexcept { return d_8571; }

    // The live plane-C collectible value at a cell (0 once collected), for the
    // renderer to draw only what remains. cell = row*8 + col.
    [[nodiscard]] std::uint8_t collectible(int col, int row) const;
    // Whole live grid (a0d8: planes A/B/C + header) plus trailing slack, for the
    // renderer. Only [0, 0x96) is meaningful.
    [[nodiscard]] const std::array<std::uint8_t, 0x100>& grid() const noexcept { return grid_; }

    // Currently-running tile bump/spring animations (the pegs/blocks reacting to
    // the ball), for the renderer to overlay. At most 3 layer-A + 4 layer-B run at
    // once. Fills `out` and returns the count. A frame_index of kAnimHiddenFrame is
    // a blink-off step (draw nothing). See object_anim.h / FUN_1000_14e4/15a1.
    std::size_t object_anims(std::array<ObjectAnimSprite, 7>& out) const;

    // Drain the sound events queued since the last call (each recovered FUN_1000_6e11
    // site's speaker-profile SFX id). Moves + clears the internal queue: call once per
    // tick() from the platform shell and feed each id to AudioEngine::play_sfx.
    std::vector<std::uint8_t> take_sfx_events();

private:
    // --- mirrored DS:0x* globals (kept named for 1:1 auditability) ---
    // 0x96 live bytes (3 planes A/B/C + header) padded to 0x100 so an out-of-range
    // cell index (e.g. a +0x60 plane-C read at a stray cell) can't read past the end.
    std::array<std::uint8_t, 0x100> grid_{};  // a0d8
    BallMotion ball_{};                      // 792c/824d/792a/9bae/8242/824a/9290/9292/856e/855c/855e

    // One object-animation slot (a DS:0x4c70 / 0x4cbc record). While active it
    // plays a sprite-index byte stream, one step per frame (FUN_1000_14e4/15a1).
    struct AnimSlot {
        bool active{};                                 // record[0]
        std::uint8_t cell{};                           // record[1] -- grid cell 0..47
        const std::uint8_t* stream{};                  // record[2..5] -- stream cursor base
        std::uint8_t cursor{};                         // bytes consumed from stream
        std::uint16_t frame_index{kAnimHiddenFrame};   // current sprite (record[10] -> frame)
        std::uint8_t y_offset{};                       // current Y anchor (record[8])
    };
    std::array<AnimSlot, 3> anim_a_{};  // layer-A peg slots  (4c70)
    std::array<AnimSlot, 4> anim_b_{};  // layer-B block slots (4cbc)

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

    // --- nest spin / cushion bob frame cycling (FUN_1000_495c) ---
    std::uint8_t d_855d{};   // 495c tick counter
    std::uint8_t d_a0dc{};   // 495c frame-cycle index

    // --- picture-block match cascade (FUN_1000_6183 / 629c) ---
    // The DS:0x886 cell list (0xff-terminated) + the 9ba6 cursor and the 79b7
    // inter-pop delay: once every picture block (plane-B 0x0e..0x11) shows the same
    // art, the 0x05 blocks pop open one at a time.
    std::array<std::uint8_t, 0x31> d_0886_{};
    std::uint8_t cascade_cursor_{};  // 9ba6 (kept as an index, not a far pointer)
    std::uint8_t d_79b7{};

    // --- moving entity (monster): DS:0x856x / 0x79bx, only board-2 has one ---
    std::uint8_t d_8562{};   // current movement-script id (also the AI-dispatch index)
    std::uint8_t d_9d2f{};   // script direction flag (mirrors dx)
    std::uint8_t d_a1b0{};   // keyframes remaining in the current script
    std::uint8_t d_8563{};   // step counter within the current script (mid-step at 5)
    std::uint8_t d_8560{};   // current sprite keyframe (0..3); frame = a0de + this
    std::uint8_t d_8243{};   // half-rate toggle: the entity steps every other frame
    std::uint8_t d_8564{};   // entity col (0..7)
    std::uint8_t d_8565{};   // entity row (0..5)
    int d_79ba{};            // entity pixel x (cell slot + 7)
    int d_79bc{};            // entity pixel y (cell slot + 7)
    std::uint16_t d_a0de{};  // sprite-frame base (0x2546[anim index])
    std::uint8_t d_a0e0{};   // nav free-flag UP    (0 = free)
    std::uint8_t d_a0e1{};   // nav free-flag DOWN
    std::uint8_t d_a0e2{};   // nav free-flag LEFT
    std::uint8_t d_a1b2{};   // nav free-flag RIGHT
    // AABB collision boxes (5085 ball / 50c0 entity), refreshed while a0ce==0.
    int box_ball_x0_{}, box_ball_x1_{}, box_ball_y0_{}, box_ball_y1_{};
    int box_ent_x0_{}, box_ent_x1_{}, box_ent_y0_{}, box_ent_y1_{};

    std::uint8_t d_928d{};   // quit
    std::uint8_t d_856d{};   // win
    std::uint8_t d_9d30{};   // death
    LevelStatus status_{LevelStatus::playing};

    // Sound events queued this tick (each recovered FUN_1000_6e11(id) site), drained by
    // take_sfx_events(). emit_sfx(0) is a no-op -- the tile/table lookups at the variable
    // sites (e.g. kSfxIdleRest[tile]) use 0 to mean "silent".
    std::vector<std::uint8_t> pending_sfx_;
    void emit_sfx(std::uint8_t id) {
        if (id) pending_sfx_.push_back(id);
    }

    // --- core loop ---
    std::uint16_t prng_next();                  // FUN_1000_93b1
    std::uint8_t build_input_bits(const LevelInput&) const;  // FUN_1000_75a2 (port mapping)
    void f_1d26(const LevelInput&);             // player tick
    void f_233a();                              // exit-portal pulse while the level is cleared
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

    // --- tile bump/spring animations (object_anim.h) ---
    void anim_arm(AnimSlot* slots, std::size_t n, std::uint8_t cell, const BumpEvent& ev,
                  const std::uint8_t* pool, std::size_t plane_off,
                  const AnimRecord* recs, std::size_t rec_count);
    static void anim_step(AnimSlot& s, const AnimRecord* recs, std::size_t rec_count);
    void f_69aa(std::uint8_t id);   // start a layer-A peg spring at d_856f
    void f_6a89(std::uint8_t id);   // start a layer-B block spring at d_8570
    void f_14e4();                  // step the 3 layer-A slots
    void f_15a1();                  // step the 4 layer-B slots
    void f_6987(std::uint8_t id);   // d_856f = cell; spring (idle/rest peg)
    void f_6d94(std::uint8_t id);   // d_856f = cell; spring (ball-cell peg)
    void f_6d6a(const std::uint8_t* tile_map);  // spring the lane under the ball (roll entry)
    void f_686a(std::uint8_t row);  // layer-B neighbour spring via kBumpSelectB[row]
    // The DS:0x43c0 step-0 bump entries (layer-A 6d94/6987 + layer-B 6a89 select):
    void f_6648();                  // idle/rest entry  -> 6987(kIdleSpringA)
    void f_6699();                  // hop up-left  layer-B (kBumpSelL0)
    void f_66d8();                  // hop up-right layer-B (kBumpSelR0)
    void f_6748();                  // hop up-left  6d94(0x18) + layer-B (kBumpSelL1)
    void f_6789();                  // hop up-right 6d94(0x19) + layer-B (kBumpSelR1)
    void f_6890();                  // col!=0 -> layer-B (kBumpSelL1)
    void f_68bb();                  // col!=7 -> layer-B (kBumpSelR1)
    void f_6326();                  // roll-left spike check: plane-B 0x0c -> death tumble
    void f_6372();                  // roll-right spike check: plane-B 0x0c -> death tumble

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
    void f_1e3d();  // state 0x30 terminal: ball fell into the exit portal -> board cleared
    void f_22b0();  // deadly-pit / chute exit (0x12/0x1f) -> 22fc
    void f_22d2();  // death-tumble cascade: replay state 0x2e 3x, then 22fc
    void f_22fc();  // lose a life (death / deadly pit / F2 skip)
    void f_2423();  // bounce (scriptless state 5)
    void f_1fbe();  // special bumper
    void f_207d();  // special bumper
    void f_228d();  // death (entity)

    // --- nest (tile 0x16) + block-top riding (worlds 2+) ---
    void f_495c(std::uint8_t wrap, std::uint8_t period, const std::int16_t* frames);
    void f_4995(std::uint8_t wrap, const std::int16_t* frames);  // advance the cycle
    void f_4361();  // nest spin (ball frame cycle DS:0x1b70)
    void f_4305();  // enter the nest: state 0x1c + spin
    void f_1e5e();  // state 0x21: landed on a block from a hop up-left
    void f_1e90();  // state 0x22: landed on a block from a hop up-right
    void f_1ec2();  // state 0x23: sitting on a cushion block (bob; DOWN rolls off)
    void f_1f3e();  // state 0x24
    void f_1f03();  // roll off the cushion leftward (raw DS:0x140c script)
    void f_1f7f();  // roll off the cushion rightward (raw DS:0x1460 script)
    void f_2138();  // state 0x25: walking left along block tops
    void f_21e7();  // state 0x26: walking right along block tops
    void f_21bb();  // 0x25 sentinel: plane-A 0x0b check -> state 0x29/0x25
    void f_2261();  // 0x26 sentinel: plane-A 0x0b check -> state 0x2a/0x26

    // --- picture-block match puzzle (plane-B 0x0e..0x11) ---
    void f_640c();  // block-bump anim step: re-check the match puzzle
    void f_6183();  // all pictures equal? -> collect the 0x05 blocks into d_0886_
    void f_629c();  // main-loop stepper: pop one listed 0x05 block per 11 frames

    // --- moving entity (monster): movement script + maze AI + collision ---
    void f_48a9();             // init col/row + pixel pos from the entity cell
    void f_4bc6(std::uint8_t behaviour);  // load a movement script by id
    void f_4c14();             // step the movement script (every other frame)
    void f_4c99();             // on arrival: compute nav flags + dispatch AI
    void f_5003();             // mid-move: at step 5, advance the entity cell
    void f_4fd3();             // all-blocked fallback: pick a random script 5..8
    void f_5085();             // build the ball AABB box
    void f_50c0();             // build the entity AABB box
    void f_50fb();             // ball-vs-entity overlap -> sets a1aa (death pending)
    void entity_ai_arrive(std::uint8_t behaviour);  // DS:0x870 on-arrival dispatch
    void entity_ai_mid(std::uint8_t behaviour);      // DS:0x85c mid-step dispatch
    void f_4dbf();  // arrive AI: was UP    (up>right>left>down)
    void f_4e44();  // arrive AI: was DOWN  (down>left>right>up)
    void f_4ec9();  // arrive AI: was LEFT  (left>up>down>right)
    void f_4f4e();  // arrive AI: was RIGHT (right>down>up>left)
    void f_4dfa();  // leaf: commit UP (or a random detour when 79b3 < 7920)
    void f_4e7f();  // leaf: commit DOWN
    void f_4f04();  // leaf: commit LEFT
    void f_4f89();  // leaf: commit RIGHT
    void f_5025();  // mid-step cell move UP   (cell -= 8)
    void f_503f();  // mid-step cell move DOWN (cell += 8)
    void f_5059();  // mid-step cell move LEFT (cell -= 1)
    void f_506f();  // mid-step cell move RIGHT(cell += 1)

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

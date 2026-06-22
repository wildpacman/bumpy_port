#pragma once

#include <cstdint>

namespace bumpy {

// The plane-A tile the ball rests on selects how it reacts. The original reads a
// reaction code from one of five consecutive 48-byte tables (DS:0x36be..0x377e)
// and maps it to an action with FUN_1000_46bb. The table is chosen by input
// direction (and one variant by player state). See analysis/specs/tile-semantics.md.
//
// Values are byte offsets into the baked contiguous block (tile_reactions.gen.cpp),
// i.e. the table base minus 0x36be.
enum class TileReactionTable : int {
    none = 0x00,        // 0x36be: no vertical input (FUN_1000_465e)
    up = 0x30,          // 0x36ee: UP pressed       (FUN_1000_467d)
    down = 0x60,        // 0x371e: DOWN pressed      (FUN_1000_469c)
    roll_above = 0x90,  // 0x374e: indexed by the cell ABOVE during a roll (FUN_1000_4747)
    on_0a = 0xc0,       // 0x377e: on a 0x0a special lane, indexed by player STATE (FUN_1000_47cb)
};

// Raw reaction code from `table` at `index` (a plane-A tile value, or a player
// state for TileReactionTable::on_0a). Faithful to the original's unchecked
// `*(index + base)` read; out-of-range indices return 0 (roll).
[[nodiscard]] std::uint8_t tile_reaction_code(TileReactionTable table, std::uint8_t index);

// The action a reaction code maps to (FUN_1000_46bb).
enum class TileAction {
    roll,          // 0    -> FUN_1000_4747 (roll, or idle-blink when the cell above is open)
    hop_up_left,   // 1    -> FUN_1000_2634
    hop_up_right,  // 2    -> FUN_1000_26a1
    fall,          // 3    -> FUN_1000_27de
    bounce_270c,   // 8    -> FUN_1000_270c
    bounce_2776,   // 9    -> FUN_1000_2776
    special_1fbe,  // 0x1a -> FUN_1000_1fbe
    special_207d,  // 0x1b -> FUN_1000_207d
    set_state,     // any other code: the code becomes the new player state (FUN_1000_4263)
};

struct TileReaction {
    TileAction action{};
    std::uint8_t state{};  // the new state when action == set_state

    friend bool operator==(const TileReaction&, const TileReaction&) = default;
};

// Decode a raw reaction code into an action, mirroring FUN_1000_46bb's switch.
[[nodiscard]] TileReaction decode_tile_action(std::uint8_t code);

}  // namespace bumpy

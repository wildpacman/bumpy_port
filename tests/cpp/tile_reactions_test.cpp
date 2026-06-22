#include "game/tile_reactions.h"

#include <catch2/catch_test_macros.hpp>

using namespace bumpy;

namespace {

TileReaction react(TileReactionTable table, std::uint8_t value) {
    return decode_tile_action(tile_reaction_code(table, value));
}

constexpr TileReaction roll{TileAction::roll, 0};
constexpr TileReaction hop_ul{TileAction::hop_up_left, 0};
constexpr TileReaction hop_ur{TileAction::hop_up_right, 0};
constexpr TileReaction fall{TileAction::fall, 0};
constexpr TileReaction s1fbe{TileAction::special_1fbe, 0};
constexpr TileReaction s207d{TileAction::special_207d, 0};
TileReaction state(std::uint8_t s) { return {TileAction::set_state, s}; }

}  // namespace

// Every row here is transcribed from analysis/specs/tile-semantics.md's decoded-D1
// table; it pins both the baked reaction block and the FUN_1000_46bb decode.
TEST_CASE("plane-A lanes roll and hop per the tile-semantics table") {
    // 0x01 basic lane (and the other plain lanes behave the same).
    for (std::uint8_t lane : {0x01, 0x02, 0x07, 0x08, 0x09, 0x0a, 0x10}) {
        INFO("lane 0x" << std::hex << static_cast<int>(lane));
        CHECK(react(TileReactionTable::none, lane) == roll);
        CHECK(react(TileReactionTable::up, lane) == hop_ul);
        CHECK(react(TileReactionTable::down, lane) == hop_ur);
        CHECK(react(TileReactionTable::roll_above, lane) == roll);
    }
}

TEST_CASE("deflectors always hop the same direction") {
    // 0x05 left deflector: hop up-left regardless of input; ceiling above -> state 0x11.
    CHECK(react(TileReactionTable::none, 0x05) == hop_ul);
    CHECK(react(TileReactionTable::up, 0x05) == hop_ul);
    CHECK(react(TileReactionTable::down, 0x05) == hop_ul);
    CHECK(react(TileReactionTable::roll_above, 0x05) == state(0x11));
    // 0x06 right deflector.
    CHECK(react(TileReactionTable::none, 0x06) == hop_ur);
    CHECK(react(TileReactionTable::up, 0x06) == hop_ur);
    CHECK(react(TileReactionTable::down, 0x06) == hop_ur);
    CHECK(react(TileReactionTable::roll_above, 0x06) == state(0x11));
}

TEST_CASE("hole, fall-through and ceiling lane reactions") {
    // 0x0f hole: drop through with no vertical input (state 0xe), else hop.
    CHECK(react(TileReactionTable::none, 0x0f) == state(0x0e));
    CHECK(react(TileReactionTable::up, 0x0f) == hop_ul);
    CHECK(react(TileReactionTable::down, 0x0f) == hop_ur);
    CHECK(react(TileReactionTable::roll_above, 0x0f) == roll);

    // 0x19 always falls; the cell above keeps rolling.
    CHECK(react(TileReactionTable::none, 0x19) == fall);
    CHECK(react(TileReactionTable::up, 0x19) == fall);
    CHECK(react(TileReactionTable::down, 0x19) == fall);
    CHECK(react(TileReactionTable::roll_above, 0x19) == roll);

    // 0x1e lane that bounces off the ceiling above (state 0x11) rather than rolling.
    CHECK(react(TileReactionTable::none, 0x1e) == roll);
    CHECK(react(TileReactionTable::up, 0x1e) == hop_ul);
    CHECK(react(TileReactionTable::down, 0x1e) == hop_ur);
    CHECK(react(TileReactionTable::roll_above, 0x1e) == state(0x11));
}

TEST_CASE("trigger tiles and special bumpers") {
    // 0x12 / 0x1f: any horizontal contact triggers state 0x10.
    for (auto t : {TileReactionTable::none, TileReactionTable::up, TileReactionTable::down}) {
        CHECK(react(t, 0x12) == state(0x10));
        CHECK(react(t, 0x1f) == state(0x10));
    }
    CHECK(react(TileReactionTable::roll_above, 0x12) == state(0x11));
    CHECK(react(TileReactionTable::roll_above, 0x1f) == roll);

    // 0x14 / 0x15 special bounces (1fbe / 207d); ceiling above -> state 0x11.
    for (auto t : {TileReactionTable::none, TileReactionTable::up, TileReactionTable::down}) {
        CHECK(react(t, 0x14) == s1fbe);
        CHECK(react(t, 0x15) == s207d);
    }
    CHECK(react(TileReactionTable::roll_above, 0x14) == state(0x11));
    CHECK(react(TileReactionTable::roll_above, 0x15) == state(0x11));
}

TEST_CASE("46bb decode covers all fixed action codes") {
    CHECK(decode_tile_action(0x00) == roll);
    CHECK(decode_tile_action(0x01) == hop_ul);
    CHECK(decode_tile_action(0x02) == hop_ur);
    CHECK(decode_tile_action(0x03) == fall);
    CHECK(decode_tile_action(0x08) == TileReaction{TileAction::bounce_270c, 0});
    CHECK(decode_tile_action(0x09) == TileReaction{TileAction::bounce_2776, 0});
    CHECK(decode_tile_action(0x1a) == s1fbe);
    CHECK(decode_tile_action(0x1b) == s207d);
    // Codes 4..7 and >=0xc that are not special all become "set this state".
    CHECK(decode_tile_action(0x04) == state(0x04));
    CHECK(decode_tile_action(0x10) == state(0x10));
    CHECK(decode_tile_action(0x2e) == state(0x2e));
}

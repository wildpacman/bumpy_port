#include "game/tile_reactions.h"

namespace bumpy {

// FUN_1000_46bb: map a reaction code to an action. Codes 0..3 and 8/9/0x1a/0x1b
// are fixed actions; every other code becomes the new player state (FUN_1000_472d
// -> FUN_1000_4263(code)).
TileReaction decode_tile_action(std::uint8_t code) {
    switch (code) {
    case 0x00:
        return {TileAction::roll, 0};
    case 0x01:
        return {TileAction::hop_up_left, 0};
    case 0x02:
        return {TileAction::hop_up_right, 0};
    case 0x03:
        return {TileAction::fall, 0};
    case 0x08:
        return {TileAction::bounce_270c, 0};
    case 0x09:
        return {TileAction::bounce_2776, 0};
    case 0x1a:
        return {TileAction::special_1fbe, 0};
    case 0x1b:
        return {TileAction::special_207d, 0};
    default:
        return {TileAction::set_state, code};
    }
}

}  // namespace bumpy

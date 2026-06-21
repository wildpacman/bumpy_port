#include <catch2/catch_test_macros.hpp>

#include "game/world_map.h"

#include <algorithm>

using bumpy::MenuInput;
using bumpy::WorldMap;
using bumpy::WorldMapResult;

namespace {
// Drive one full action: press the key (which may start a slide or the fire jump), run
// any resulting animation to completion, then release so the next press registers past
// the debounce. Fire returns select_board only once the jump finishes, so capture the
// action that emerges at the end of the animation rather than the first tick's.
bumpy::WorldMapAction act(WorldMap& map, const MenuInput& input) {
    auto action = map.update(input);
    int guard = 0;
    while ((map.is_sliding() || map.is_jumping()) && guard++ < 1000) {
        const auto stepped = map.update(MenuInput{});
        if (stepped.result != WorldMapResult::none) {
            action = stepped;
        }
    }
    map.update(MenuInput{});  // release
    return action;
}
}  // namespace

TEST_CASE("world map starts on node 1 at its baked position") {
    WorldMap map;
    REQUIRE(map.current_node() == 1);
    REQUIRE(map.view().avatar_x == 32);
    REQUIRE(map.view().avatar_y == 32);
    REQUIRE(map.node_count() == 15);
    REQUIRE_FALSE(map.is_sliding());
}

TEST_CASE("baked world-1 table matches screen-flow.md") {
    REQUIRE(bumpy::world1_node_count() == 15);
    // node 1: right -> 2 only.
    REQUIRE(bumpy::world1_node(1).right == 2);
    REQUIRE(bumpy::world1_node(1).up == 0);
    REQUIRE(bumpy::world1_node(1).down == 0);
    REQUIRE(bumpy::world1_node(1).left == 0);
    // node 8: U5 D12 R9.
    REQUIRE(bumpy::world1_node(8).up == 5);
    REQUIRE(bumpy::world1_node(8).down == 12);
    REQUIRE(bumpy::world1_node(8).right == 9);
    // node 15: U11 L14, at (272,176).
    REQUIRE(bumpy::world1_node(15).up == 11);
    REQUIRE(bumpy::world1_node(15).left == 14);
    REQUIRE(bumpy::world1_node(15).x == 272);
    REQUIRE(bumpy::world1_node(15).y == 176);
}

TEST_CASE("an arrow with no linked neighbour is a no-op") {
    WorldMap map;  // node 1 links right only
    REQUIRE(act(map, MenuInput{.up = true}).result == WorldMapResult::none);
    REQUIRE(act(map, MenuInput{.left = true}).result == WorldMapResult::none);
    REQUIRE(map.current_node() == 1);
    REQUIRE_FALSE(map.is_sliding());
}

TEST_CASE("arrows walk the graph to linked neighbours") {
    WorldMap map;
    act(map, MenuInput{.right = true});  // 1 -> 2
    REQUIRE(map.current_node() == 2);
    REQUIRE(map.view().avatar_x == 112);
    REQUIRE(map.view().avatar_y == 32);
    act(map, MenuInput{.down = true});   // 2 -> 9
    REQUIRE(map.current_node() == 9);
    REQUIRE(map.view().avatar_x == 112);
    REQUIRE(map.view().avatar_y == 128);
    act(map, MenuInput{.left = true});   // 9 -> 8
    REQUIRE(map.current_node() == 8);
}

TEST_CASE("pressing an arrow glides the avatar 4px/tick to the neighbour") {
    WorldMap map;  // node 1 at (32,32); right -> node 2 at (112,32)

    // The press starts the slide: the logical node updates at once, the avatar does
    // not -- it is still at the origin and now sliding.
    REQUIRE(map.update(MenuInput{.right = true}).result == WorldMapResult::none);
    REQUIRE(map.is_sliding());
    REQUIRE(map.current_node() == 2);
    REQUIRE(map.view().avatar_x == 32);

    // Each tick advances 4px along the connecting line (y unchanged -> moves on it).
    map.update(MenuInput{});
    REQUIRE(map.view().avatar_x == 36);
    REQUIRE(map.view().avatar_y == 32);

    int guard = 0;
    while (map.is_sliding() && guard++ < 1000) {
        map.update(MenuInput{});
    }
    REQUIRE(map.view().avatar_x == 112);
    REQUIRE(map.view().avatar_y == 32);
    REQUIRE_FALSE(map.is_sliding());
}

TEST_CASE("input is ignored while the avatar is sliding") {
    WorldMap map;
    map.update(MenuInput{.right = true});  // start sliding toward node 2
    REQUIRE(map.is_sliding());

    // Fire mid-slide must do nothing and not interrupt the glide.
    const auto action = map.update(MenuInput{.confirm = true});
    REQUIRE(action.result == WorldMapResult::none);
    REQUIRE(map.is_sliding());
    REQUIRE(map.current_node() == 2);
}

TEST_CASE("fire selects the current node's board (node - 1)") {
    WorldMap map;
    act(map, MenuInput{.right = true});  // slide to node 2
    const auto action = act(map, MenuInput{.confirm = true});
    REQUIRE(action.result == WorldMapResult::select_board);
    REQUIRE(action.board_index == 1);  // node 2 -> board 1
}

TEST_CASE("cancel returns to the menu") {
    WorldMap map;
    REQUIRE(act(map, MenuInput{.cancel = true}).result == WorldMapResult::back_to_menu);
}

TEST_CASE("fire plays the cloud-jump animation before selecting the board") {
    WorldMap map;  // node 1

    // The fire tick starts the jump (FUN_1000_3cf7): the cloud shows and the first ball
    // frame is posed immediately, but no board is selected yet.
    const auto first = map.update(MenuInput{.confirm = true});
    REQUIRE(first.result == WorldMapResult::none);
    REQUIRE(map.is_jumping());
    REQUIRE(map.view().cloud_visible);
    REQUIRE(map.view().avatar_frame == 0);        // pre-roll frame 0
    REQUIRE(map.view().avatar_offset_y == 0);

    // The animation is purely vertical and bounces up before arcing down: the avatar
    // reaches its highest point (offset -8) and ends below the start (offset +8).
    int min_offset = 0;
    int max_offset = 0;
    bool saw_stretched_frame = false;  // frames 0x13.. are the taller falling poses
    int guard = 0;
    while (map.is_jumping() && guard++ < 1000) {
        const auto stepped = map.update(MenuInput{});
        min_offset = std::min(min_offset, map.view().avatar_offset_y);
        max_offset = std::max(max_offset, map.view().avatar_offset_y);
        if (map.view().avatar_frame >= 0x13 && map.view().avatar_frame <= 0x1f) {
            saw_stretched_frame = true;
        }
        // Board selection only happens on the tick the animation finishes.
        if (stepped.result != WorldMapResult::none) {
            REQUIRE_FALSE(map.is_jumping());
            REQUIRE(stepped.result == WorldMapResult::select_board);
            REQUIRE(stepped.board_index == 0);  // node 1 -> board 0
        }
    }
    REQUIRE(min_offset == -8);
    REQUIRE(max_offset == 8);
    REQUIRE(saw_stretched_frame);

    // Once done the avatar is reset to its resting pose with the cloud gone.
    REQUIRE_FALSE(map.is_jumping());
    REQUIRE(map.view().avatar_frame == bumpy::kRestingAvatarFrame);
    REQUIRE(map.view().avatar_offset_y == 0);
    REQUIRE_FALSE(map.view().cloud_visible);
}

TEST_CASE("fire is ignored mid-jump and the jump finishes regardless of input") {
    WorldMap map;
    map.update(MenuInput{.confirm = true});  // start the jump
    REQUIRE(map.is_jumping());

    // Holding any key during the jump neither cancels nor reroutes it.
    int guard = 0;
    WorldMapResult final_result = WorldMapResult::none;
    while (map.is_jumping() && guard++ < 1000) {
        const auto stepped = map.update(MenuInput{.confirm = true, .cancel = true});
        if (stepped.result != WorldMapResult::none) {
            final_result = stepped.result;
        }
    }
    REQUIRE(final_result == WorldMapResult::select_board);
}

TEST_CASE("a held arrow advances only one node per press") {
    WorldMap map;
    REQUIRE(map.update(MenuInput{.right = true}).result == WorldMapResult::none);
    REQUIRE(map.current_node() == 2);
    // Hold right through the whole slide: the held key is ignored mid-slide...
    int guard = 0;
    while (map.is_sliding() && guard++ < 1000) {
        map.update(MenuInput{.right = true});
    }
    // ...and once arrived, the still-held key is debounced -- no second slide.
    map.update(MenuInput{.right = true});
    REQUIRE(map.current_node() == 2);
    REQUIRE_FALSE(map.is_sliding());

    // Release, then a fresh right press: node 2 has no right link -> no move.
    map.update(MenuInput{});
    map.update(MenuInput{.right = true});
    REQUIRE(map.current_node() == 2);
}

TEST_CASE("enter resets to node 1 (snap, no slide) and requires a key release first") {
    WorldMap map;
    act(map, MenuInput{.right = true});  // node 2
    map.enter();
    REQUIRE(map.current_node() == 1);
    REQUIRE(map.view().avatar_x == 32);
    REQUIRE_FALSE(map.is_sliding());
    // Holding confirm across enter must not select until released.
    REQUIRE(map.update(MenuInput{.confirm = true}).result == WorldMapResult::none);
    map.update(MenuInput{});  // release
    // A fresh confirm plays the cloud-jump animation, then selects the board.
    REQUIRE(act(map, MenuInput{.confirm = true}).result == WorldMapResult::select_board);
}

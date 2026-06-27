#include <catch2/catch_test_macros.hpp>

#include "game/world_map.h"
#include "game/world_graphs.h"

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

    // The press starts the slide: the logical node updates at once and the avatar takes
    // its first 4px step on the same tick (FUN_1000_3ab2 moves before the first retrace).
    REQUIRE(map.update(MenuInput{.right = true}).result == WorldMapResult::none);
    REQUIRE(map.is_sliding());
    REQUIRE(map.current_node() == 2);
    REQUIRE(map.view().avatar_x == 36);
    REQUIRE(map.view().avatar_y == 32);

    // Each further tick advances another 4px along the connecting line.
    map.update(MenuInput{});
    REQUIRE(map.view().avatar_x == 40);
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

TEST_CASE("a held arrow walks node to node continuously (no release needed)") {
    WorldMap map;
    // Navigate to node 12 (single presses): 1 -R-> 2 -D-> 9 -L-> 8 -D-> 12. Nodes
    // 12,13,14,15 form a horizontal row each linked right to the next.
    act(map, MenuInput{.right = true});
    act(map, MenuInput{.down = true});
    act(map, MenuInput{.left = true});
    act(map, MenuInput{.down = true});
    REQUIRE(map.current_node() == 12);

    // Hold right and never release: the avatar should glide 12 -> 13 -> 14 -> 15, starting
    // each next slide the instant the previous one lands (the original re-polls the held
    // key every loop iteration; the slide is the only pacing -- no per-press debounce).
    int guard = 0;
    while (map.current_node() != 15 && guard++ < 5000) {
        map.update(MenuInput{.right = true});
    }
    REQUIRE(map.current_node() == 15);

    // Node 15 has no right link: holding right past it stops cleanly (no wrap, no slide).
    for (int i = 0; i < 50; ++i) {
        map.update(MenuInput{.right = true});
    }
    REQUIRE(map.current_node() == 15);
    REQUIRE_FALSE(map.is_sliding());
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

TEST_CASE("WorldMap can be constructed for a later world") {
    bumpy::WorldMap map(2);
    REQUIRE(map.world() == 2);
    REQUIRE(map.view().world == 2);
    REQUIRE(map.current_node() == 1);
    // World 2 node 1 sits at (112, 32) (the start node differs from world 1's (32,32)).
    REQUIRE(map.view().avatar_x == 112);
    REQUIRE(map.view().avatar_y == 32);
    REQUIRE(map.node_count() == static_cast<std::size_t>(bumpy::world_node_count(2)));
}

TEST_CASE("load_world switches the graph and snaps to node 1") {
    bumpy::WorldMap map;  // world 1
    map.load_world(4);
    REQUIRE(map.world() == 4);
    REQUIRE(map.view().world == 4);
    REQUIRE(map.current_node() == 1);
    REQUIRE(map.node_count() == 12);  // world 4 has 12 nodes
    REQUIRE_FALSE(map.is_sliding());
}

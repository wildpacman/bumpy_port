#include <catch2/catch_test_macros.hpp>

#include "game/world_map.h"

using bumpy::MenuInput;
using bumpy::WorldMap;
using bumpy::WorldMapResult;

namespace {
// Drive one full action: press the key (which may start a slide), run any resulting
// slide to completion, then release so the next press registers past the debounce.
bumpy::WorldMapAction act(WorldMap& map, const MenuInput& input) {
    const auto action = map.update(input);
    int guard = 0;
    while (map.is_sliding() && guard++ < 1000) {
        map.update(MenuInput{});
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
    REQUIRE(map.update(MenuInput{.confirm = true}).result == WorldMapResult::select_board);
}

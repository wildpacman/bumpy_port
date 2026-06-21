#include <catch2/catch_test_macros.hpp>

#include "game/world_map.h"

using bumpy::MenuInput;
using bumpy::WorldMap;
using bumpy::WorldMapResult;

namespace {
// Press a key for one frame, then release, so the WorldMap debounce sees one edge.
bumpy::WorldMapAction tap(WorldMap& map, const MenuInput& input) {
    const auto action = map.update(input);
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
    REQUIRE(tap(map, MenuInput{.up = true}).result == WorldMapResult::none);
    REQUIRE(tap(map, MenuInput{.left = true}).result == WorldMapResult::none);
    REQUIRE(map.current_node() == 1);
}

TEST_CASE("arrows walk the graph to linked neighbours") {
    WorldMap map;
    tap(map, MenuInput{.right = true});  // 1 -> 2
    REQUIRE(map.current_node() == 2);
    REQUIRE(map.view().avatar_x == 112);
    REQUIRE(map.view().avatar_y == 32);
    tap(map, MenuInput{.down = true});   // 2 -> 9
    REQUIRE(map.current_node() == 9);
    tap(map, MenuInput{.left = true});   // 9 -> 8
    REQUIRE(map.current_node() == 8);
}

TEST_CASE("fire selects the current node's board (node - 1)") {
    WorldMap map;
    tap(map, MenuInput{.right = true});  // node 2
    const auto action = tap(map, MenuInput{.confirm = true});
    REQUIRE(action.result == WorldMapResult::select_board);
    REQUIRE(action.board_index == 1);  // node 2 -> board 1
}

TEST_CASE("cancel returns to the menu") {
    WorldMap map;
    REQUIRE(tap(map, MenuInput{.cancel = true}).result == WorldMapResult::back_to_menu);
}

TEST_CASE("a held arrow advances only one node per press") {
    WorldMap map;
    REQUIRE(map.update(MenuInput{.right = true}).result == WorldMapResult::none);
    REQUIRE(map.current_node() == 2);
    // Still holding right: must not advance again until release.
    map.update(MenuInput{.right = true});
    REQUIRE(map.current_node() == 2);
    map.update(MenuInput{});                 // release
    map.update(MenuInput{.right = true});    // node 2 has no right link -> no move
    REQUIRE(map.current_node() == 2);
}

TEST_CASE("enter resets to node 1 and requires a key release first") {
    WorldMap map;
    tap(map, MenuInput{.right = true});  // node 2
    map.enter();
    REQUIRE(map.current_node() == 1);
    // Holding confirm across enter must not select until released.
    REQUIRE(map.update(MenuInput{.confirm = true}).result == WorldMapResult::none);
    map.update(MenuInput{});  // release
    REQUIRE(map.update(MenuInput{.confirm = true}).result == WorldMapResult::select_board);
}

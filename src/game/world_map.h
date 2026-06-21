#pragma once

#include "game/menu.h"  // MenuInput

#include <cstddef>
#include <cstdint>

namespace bumpy {

enum class WorldMapResult { none, select_board, back_to_menu };

struct WorldMapAction {
    WorldMapResult result{WorldMapResult::none};
    std::size_t board_index{};  // valid when result == select_board (= current_node - 1)
};

struct WorldMapView {
    int current_node{1};  // 1-based
    int avatar_x{};
    int avatar_y{};
};

// One world-map node: linked neighbour node numbers (1-based; 0 = no link) and the
// node's pixel position. Baked from BUMPY.UNPACKED.EXE world-1 tables (graph
// DS:0x09e6, positions DS:0x0a80); see analysis/specs/screen-flow.md and
// docs/superpowers/specs/2026-06-21-world-map-screen-design.md.
struct MapNode {
    std::uint8_t up{};
    std::uint8_t down{};
    std::uint8_t left{};
    std::uint8_t right{};
    int x{};
    int y{};
};

// SDL-independent world-map state machine (world 1). Owns the current node and the
// avatar position; navigation snaps to linked neighbours. Debounces its own input
// (one action per key press) like Menu does. App drives it on Screen::map.
class WorldMap {
public:
    WorldMap();  // world 1, avatar on node 1

    // Reset to node 1 and require all keys released before the next action. App
    // calls this on each menu->map entry so a held fire/cancel cannot carry across.
    void enter() noexcept;

    WorldMapAction update(const MenuInput& input) noexcept;

    [[nodiscard]] const WorldMapView& view() const noexcept { return view_; }
    [[nodiscard]] int current_node() const noexcept { return view_.current_node; }
    [[nodiscard]] std::size_t node_count() const noexcept;

private:
    void move_to(int node) noexcept;  // set current node + avatar position

    WorldMapView view_{};
    bool waiting_for_release_{false};  // a fresh map acts on first input; enter() arms the guard
};

// The baked world-1 node table (index 0 is an unused sentinel; nodes 1..15).
[[nodiscard]] const MapNode& world1_node(int node);
[[nodiscard]] int world1_node_count() noexcept;  // 15

}  // namespace bumpy

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
// avatar position. Pressing a direction with a linked neighbour starts a slide: the
// avatar glides 4px/tick along the connecting line to the neighbour (the original's
// FUN_1000_3ab2..3bc9 animate dist>>2 steps of 4px), and input is ignored until it
// arrives -- so update() must be called every tick. Debounces its own input (one
// slide/action per key press) like Menu does. App drives it on Screen::map.
class WorldMap {
public:
    WorldMap();  // world 1, avatar on node 1

    // Reset to node 1 (snap, no slide) and require all keys released before the next
    // action. App calls this on each menu->map entry so a held fire/cancel cannot
    // carry across.
    void enter() noexcept;

    WorldMapAction update(const MenuInput& input) noexcept;

    [[nodiscard]] const WorldMapView& view() const noexcept { return view_; }
    [[nodiscard]] int current_node() const noexcept { return view_.current_node; }
    [[nodiscard]] std::size_t node_count() const noexcept;
    // True while the avatar is gliding between nodes (input is ignored meanwhile).
    [[nodiscard]] bool is_sliding() const noexcept { return sliding_; }

private:
    void move_to(int node) noexcept;     // snap current node + avatar position
    void start_slide(int node) noexcept; // set target node; begin the glide
    void advance_slide() noexcept;       // step the avatar 4px toward the target

    WorldMapView view_{};
    bool waiting_for_release_{false};  // a fresh map acts on first input; enter() arms the guard
    bool sliding_{false};
    int slide_to_x_{};
    int slide_to_y_{};
};

// The baked world-1 node table (index 0 is an unused sentinel; nodes 1..15).
[[nodiscard]] const MapNode& world1_node(int node);
[[nodiscard]] int world1_node_count() noexcept;  // 15

}  // namespace bumpy

#include "game/world_map.h"

#include <array>

namespace bumpy {
namespace {

// World-1 node graph + positions, extracted from BUMPY.UNPACKED.EXE: graph base
// DS:0x09e6 (file 0x11E26), node N record at base + N*9; positions base DS:0x0a80
// (file 0x11EC0), (x,y) little-endian words at (N-1)*4. Records are
// {state, up_nbr, up_dist, down_nbr, down_dist, left_nbr, left_dist, right_nbr,
// right_dist}; only the neighbour node numbers are needed (snap navigation). Links
// verified against analysis/specs/screen-flow.md. Index 0 is the original's zero
// node-0 padding slot. Fields: {up, down, left, right, x, y}.
constexpr std::array<MapNode, 16> kWorld1{{
    {0, 0, 0, 0, 0, 0},        // 0: unused sentinel
    {0, 0, 0, 2, 32, 32},      // 1: R2
    {0, 9, 1, 0, 112, 32},     // 2: L1 D9
    {0, 0, 0, 4, 192, 32},     // 3: R4
    {0, 7, 3, 0, 272, 32},     // 4: L3 D7
    {0, 8, 0, 0, 32, 80},      // 5: D8
    {0, 10, 0, 7, 192, 80},    // 6: D10 R7
    {4, 0, 6, 0, 272, 80},     // 7: U4 L6
    {5, 12, 0, 9, 32, 128},    // 8: U5 D12 R9
    {2, 0, 8, 0, 112, 128},    // 9: U2 L8
    {6, 0, 0, 11, 192, 128},   // 10: U6 R11
    {0, 15, 10, 0, 272, 128},  // 11: L10 D15
    {8, 0, 0, 13, 32, 176},    // 12: U8 R13
    {0, 0, 12, 14, 112, 176},  // 13: L12 R14
    {0, 0, 13, 15, 192, 176},  // 14: L13 R15
    {11, 0, 14, 0, 272, 176},  // 15: U11 L14
}};

}  // namespace

const MapNode& world1_node(int node) {
    return kWorld1[static_cast<std::size_t>(node)];
}

int world1_node_count() noexcept {
    return 15;
}

WorldMap::WorldMap() {
    move_to(1);
    waiting_for_release_ = false;
}

void WorldMap::enter() noexcept {
    waiting_for_release_ = true;
    move_to(1);
}

std::size_t WorldMap::node_count() const noexcept {
    return static_cast<std::size_t>(world1_node_count());
}

void WorldMap::move_to(int node) noexcept {
    const MapNode& n = kWorld1[static_cast<std::size_t>(node)];
    view_.current_node = node;
    view_.avatar_x = n.x;
    view_.avatar_y = n.y;
    sliding_ = false;
}

void WorldMap::start_slide(int node) noexcept {
    // The logical node updates immediately (FUN_1000_3ab2 sets DAT_854e first), then
    // the avatar glides from its current pixel position to the neighbour's.
    const MapNode& n = kWorld1[static_cast<std::size_t>(node)];
    view_.current_node = node;
    slide_to_x_ = n.x;
    slide_to_y_ = n.y;
    sliding_ = view_.avatar_x != slide_to_x_ || view_.avatar_y != slide_to_y_;
}

void WorldMap::advance_slide() noexcept {
    constexpr int step = 4;  // 4px per tick; node spacing (80/48px) is a multiple of 4
    if (view_.avatar_x < slide_to_x_) {
        view_.avatar_x = view_.avatar_x + step < slide_to_x_ ? view_.avatar_x + step : slide_to_x_;
    } else if (view_.avatar_x > slide_to_x_) {
        view_.avatar_x = view_.avatar_x - step > slide_to_x_ ? view_.avatar_x - step : slide_to_x_;
    }
    if (view_.avatar_y < slide_to_y_) {
        view_.avatar_y = view_.avatar_y + step < slide_to_y_ ? view_.avatar_y + step : slide_to_y_;
    } else if (view_.avatar_y > slide_to_y_) {
        view_.avatar_y = view_.avatar_y - step > slide_to_y_ ? view_.avatar_y - step : slide_to_y_;
    }
    if (view_.avatar_x == slide_to_x_ && view_.avatar_y == slide_to_y_) {
        sliding_ = false;
    }
}

WorldMapAction WorldMap::update(const MenuInput& input) noexcept {
    if (sliding_) {
        advance_slide();  // glide toward the target node; input is ignored mid-slide
        return {};
    }

    const bool any = input.up || input.down || input.left || input.right ||
                     input.confirm || input.cancel;
    if (!any) {
        waiting_for_release_ = false;
        return {};
    }
    if (waiting_for_release_) {
        return {};
    }
    waiting_for_release_ = true;

    const MapNode& n = kWorld1[static_cast<std::size_t>(view_.current_node)];
    // Original priority (FUN_1000_3852): up, down, left, right, fire, then escape.
    if (input.up && n.up != 0) {
        start_slide(n.up);
    } else if (input.down && n.down != 0) {
        start_slide(n.down);
    } else if (input.left && n.left != 0) {
        start_slide(n.left);
    } else if (input.right && n.right != 0) {
        start_slide(n.right);
    } else if (input.confirm) {
        return {WorldMapResult::select_board, static_cast<std::size_t>(view_.current_node - 1)};
    } else if (input.cancel) {
        return {WorldMapResult::back_to_menu, 0};
    }
    return {};
}

}  // namespace bumpy

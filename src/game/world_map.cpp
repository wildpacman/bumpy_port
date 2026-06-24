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

// One displayed step of the cloud-jump animation: the avatar sprite frame and its
// vertical offset from the resting position. Recovered from FUN_1000_3cf7 and the
// script table at DS:0x1114 (BUMPY.UNPACKED.EXE file offset 0x12554): 22 records of
// {frame, dx, dy} applied one per tick by FUN_1000_13df, where avatar_offset_y is the
// running sum of dy (dx is 0 for every record -> purely vertical). The two leading
// frame-0 entries are the pre-loop draws 3cf7 issues before the script loop. The final
// frame (kAvatarFrameHidden) is the script's frame 100, which the blitter skips so the
// avatar vanishes as the board loads.
struct JumpFrame {
    int frame;
    int offset_y;
};
constexpr std::array<JumpFrame, 24> kJump{{
    {0, 0},   {0, 0},                                            // pre-loop: frame 0, in place
    {1, 0},   {2, 0},  {3, 0},  {4, 0},                          // records 0-3: squash in place
    {5, 0},   {6, 0},  {7, 0},  {0, 0},                          // records 4-7
    {1, -3},  {2, -5}, {3, -7}, {4, -8},                         // records 8-11: bounce up
    {5, -8},  {6, -7}, {7, -6}, {0, -5},                         // records 12-15
    {0x13, -4}, {0x16, -1}, {0x19, 2}, {0x1c, 5}, {0x1f, 8},     // records 16-20: arc down (stretched)
    {kAvatarFrameHidden, 8},                                     // record 21: vanish
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
    clear_jump();
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
    if (sliding_) {
        // The original takes the first 4px step on the same tick the move begins
        // (FUN_1000_3ab2's loop does `DAT_9292 -= 4` then waits for the retrace), so the
        // avatar is already 4px off the node on the first displayed frame -- no frame of
        // stale resting pose. This also means a held direction has no idle frame between
        // consecutive node slides, keeping the continuous walk smooth.
        advance_slide();
    }
}

void WorldMap::start_jump() noexcept {
    // FUN_1000_3cf7: fire on an open node plays the cloud-jump before the board loads.
    // Apply the first step on the fire tick (the original pre-draws the cloud and the
    // first pose immediately) so there is no frame of stale resting pose.
    jumping_ = true;
    view_.cloud_visible = true;
    view_.avatar_frame = kJump[0].frame;
    view_.avatar_offset_y = kJump[0].offset_y;
    jump_step_ = 1;
}

void WorldMap::clear_jump() noexcept {
    jumping_ = false;
    jump_step_ = 0;
    view_.avatar_frame = kRestingAvatarFrame;
    view_.avatar_offset_y = 0;
    view_.cloud_visible = false;
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
    if (jumping_) {
        // Play the cloud-jump one step per tick; input is ignored until it finishes.
        if (jump_step_ < kJump.size()) {
            const JumpFrame& jf = kJump[jump_step_++];
            view_.avatar_frame = jf.frame;
            view_.avatar_offset_y = jf.offset_y;
            return {};
        }
        // Animation done: reset the pose and enter the selected node's board.
        const int node = view_.current_node;
        clear_jump();
        return {WorldMapResult::select_board, static_cast<std::size_t>(node - 1)};
    }

    if (sliding_) {
        advance_slide();  // glide toward the target node; input is ignored mid-slide
        return {};
    }

    const MapNode& n = kWorld1[static_cast<std::size_t>(view_.current_node)];
    // Directions move *continuously*: the original's map loop (FUN_1000_3852) re-polls the
    // currently-held keys every iteration (FUN_1000_1dde -> FUN_1000_75a2 reads the live
    // key-state table) with no debounce, and each move animates a full node-to-node slide.
    // So holding a direction glides node by node -- the slide is the only pacing; there is
    // no per-press release requirement. Process directions before confirm/cancel to match
    // the original's input priority (up, down, left, right, fire, then escape).
    if (input.up && n.up != 0) {
        start_slide(n.up);
        return {};
    }
    if (input.down && n.down != 0) {
        start_slide(n.down);
        return {};
    }
    if (input.left && n.left != 0) {
        start_slide(n.left);
        return {};
    }
    if (input.right && n.right != 0) {
        start_slide(n.right);
        return {};
    }

    // Fire and cancel, by contrast, keep a release guard so a trigger held across a screen
    // transition cannot carry over: the menu's "start" confirm must not instantly fire into
    // a board the moment the map appears, and a held Escape must not bounce straight back to
    // the menu. enter() arms this guard on each menu->map entry.
    const bool trigger = input.confirm || input.cancel;
    if (!trigger) {
        waiting_for_release_ = false;
        return {};
    }
    if (waiting_for_release_) {
        return {};
    }
    waiting_for_release_ = true;
    if (input.confirm) {
        // All world-1 nodes are open (FUN_1000_2d14 zeroes every node's state), so fire
        // always starts the jump; select_board is returned once the animation finishes.
        start_jump();
    } else {  // input.cancel
        return {WorldMapResult::back_to_menu, 0};
    }
    return {};
}

}  // namespace bumpy

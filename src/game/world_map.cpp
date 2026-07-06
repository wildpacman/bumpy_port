#include "game/world_map.h"

#include "game/world_graphs.h"

#include <array>
#include <utility>

namespace bumpy {
namespace {


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
    return world_node(1, node);
}

int world1_node_count() noexcept {
    return world_node_count(1);
}

WorldMap::WorldMap(int world) {
    view_.world = world;
    move_to(1);
    waiting_for_release_ = false;
}

std::vector<std::uint8_t> WorldMap::take_sfx_events() {
    std::vector<std::uint8_t> events = std::move(pending_sfx_);
    pending_sfx_.clear();  // guarantee the moved-from queue is empty regardless of stdlib
    return events;
}

void WorldMap::enter() noexcept {
    waiting_for_release_ = true;
    clear_jump();
    move_to(1);
}

void WorldMap::load_world(int world) noexcept {
    view_.world = world;
    waiting_for_release_ = true;
    clear_jump();
    move_to(1);
}

std::size_t WorldMap::node_count() const noexcept {
    return static_cast<std::size_t>(world_node_count(view_.world));
}

void WorldMap::move_to(int node) noexcept {
    const MapNode& n = world_node(view_.world, node);
    view_.current_node = node;
    view_.avatar_x = n.x;
    view_.avatar_y = n.y;
    sliding_ = false;
}

void WorldMap::start_slide(int node) noexcept {
    // The logical node updates immediately (FUN_1000_3ab2 sets DAT_854e first), then
    // the avatar glides from its current pixel position to the neighbour's.
    const MapNode& n = world_node(view_.world, node);
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
    // first pose immediately) so there is no frame of stale resting pose. 3cf7 also
    // plays the launch sound (screen-flow.md: "plays a launch sound (FUN_1000_6e11)").
    emit_sfx(0x03);
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

    const MapNode& n = world_node(view_.world, view_.current_node);
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

#pragma once

#include "game/menu.h"  // MenuInput

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bumpy {

enum class WorldMapResult { none, select_board, back_to_menu };

struct WorldMapAction {
    WorldMapResult result{WorldMapResult::none};
    std::size_t board_index{};  // valid when result == select_board (= current_node - 1)
};

// The avatar's resting sprite: BUMSPJEU frame 0x21 (Bumpy-on-cloud). The fire-to-enter
// jump animation drives avatar_frame through the recovered script (see WorldMap).
inline constexpr int kRestingAvatarFrame = 0x21;
// The launch cloud sprite drawn under the bouncing ball during the jump (frame 0xcb,
// pre-drawn by FUN_1000_3cf7 at descriptor offset (-15, +3)).
inline constexpr int kJumpCloudFrame = 0xcb;
// Sentinel frame meaning "draw nothing" -- the original's blitter skips it
// (FUN_1000_1cb2: `if (DAT_824a != 100)`); the jump's last step uses it to vanish.
inline constexpr int kAvatarFrameHidden = 100;
// The completed-node marker drawn on every cleared node (FUN_1000_3c9d sets the
// descriptor to frame 0x1da at node position (x - 1, y); FUN_1000_3c4f iterates).
inline constexpr int kCompletedNodeFrame = 0x1da;

struct WorldMapView {
    int current_node{1};  // 1-based
    int world{1};         // 1-based world number (drives which node graph/positions apply)
    int avatar_x{};       // current node's pixel position (the avatar's resting anchor)
    int avatar_y{};
    int avatar_frame{kRestingAvatarFrame};  // sprite frame to draw; kAvatarFrameHidden = none
    int avatar_offset_y{};                  // vertical bounce offset added during the jump
    bool cloud_visible{};                   // draw the launch cloud (kJumpCloudFrame) while jumping
};

// One world-map node: linked neighbour node numbers (1-based; 0 = no link) and the
// node's pixel position. World-agnostic -- the per-world tables are baked into
// world_graphs.gen.cpp (graph DS:0x10c8[world], positions DS:0x10ec[world]); see
// game/world_graphs.h, analysis/specs/screen-flow.md, and
// docs/superpowers/specs/2026-06-27-worlds-2-9-design.md.
struct MapNode {
    std::uint8_t up{};
    std::uint8_t down{};
    std::uint8_t left{};
    std::uint8_t right{};
    int x{};
    int y{};
};

// SDL-independent world-map state machine (parameterized by world; default world 1).
// Owns the current node and the
// avatar position. Pressing a direction with a linked neighbour starts a slide: the
// avatar glides 4px/tick along the connecting line to the neighbour (the original's
// FUN_1000_3ab2..3bc9 animate dist>>2 steps of 4px), and input is ignored until it
// arrives -- so update() must be called every tick. Directions move *continuously*:
// the original (FUN_1000_3852) re-polls the held keys every loop iteration with no
// debounce, so holding a direction walks node to node, the slide being the only pacing.
// Only fire/cancel are release-guarded (so a trigger held across a screen transition
// cannot carry over). App drives it on Screen::map.
//
// Fire on a node plays the recovered cloud-jump animation before entering the board:
// FUN_1000_3cf7 sets up a 22-record script at DS:0x1114 (each record {frame, dx, dy},
// applied one per tick by FUN_1000_13df) plus two pre-roll frames. The avatar squashes
// on its cloud, bounces up ~8px, arcs down ~8px, then vanishes as the board loads. dx
// is 0 for every record, so the bounce is purely vertical. Input is ignored during the
// jump (like a slide); update() returns select_board only once it finishes.
class WorldMap {
public:
    explicit WorldMap(int world = 1);  // avatar on the world's node 1

    // Reset to node 1 (snap, no slide) and require all keys released before the next
    // action. App calls this on each menu->map entry so a held fire/cancel cannot
    // carry across.
    void enter() noexcept;

    // Switch to a different world's graph: snap to node 1 and arm the release guard.
    // App calls this from enter_world after the shell loads the new world's resources.
    void load_world(int world) noexcept;

    // cleared_boards (optional) is the per-board completion flags (0/1) indexed by
    // board = node-1 -- App::cleared_, the same span that draws the completed markers.
    // Fire on a node whose board is already cleared is a no-op (the original's
    // FUN_1000_3cf7 gate `if (*node_record == 0)`); an empty span means nothing is
    // cleared (every node open), matching a freshly-loaded world.
    WorldMapAction update(const MenuInput& input,
                          std::span<const std::uint8_t> cleared_boards = {}) noexcept;

    [[nodiscard]] int world() const noexcept { return view_.world; }

    [[nodiscard]] const WorldMapView& view() const noexcept { return view_; }
    [[nodiscard]] int current_node() const noexcept { return view_.current_node; }
    [[nodiscard]] std::size_t node_count() const noexcept;
    // True while the avatar is gliding between nodes (input is ignored meanwhile).
    [[nodiscard]] bool is_sliding() const noexcept { return sliding_; }
    // True while the fire-to-enter cloud-jump animation is playing (input ignored too).
    [[nodiscard]] bool is_jumping() const noexcept { return jumping_; }

    // Drain the sound events queued since the last call (the recovered FUN_1000_6e11
    // launch trigger in FUN_1000_3cf7). Moves + clears the internal queue: call once
    // per update() from the platform shell and feed each id to AudioEngine::play_sfx.
    std::vector<std::uint8_t> take_sfx_events();

private:
    void move_to(int node) noexcept;     // snap current node + avatar position
    void start_slide(int node) noexcept; // set target node; begin the glide
    void advance_slide() noexcept;       // step the avatar 4px toward the target
    void start_jump() noexcept;          // begin the cloud-jump animation on the current node
    void clear_jump() noexcept;          // reset the avatar to its resting pose

    WorldMapView view_{};
    bool waiting_for_release_{false};  // guards fire/cancel only; enter() arms it (directions are continuous)
    bool sliding_{false};
    int slide_to_x_{};
    int slide_to_y_{};
    bool jumping_{false};
    std::size_t jump_step_{0};  // index into the baked jump animation table

    // Sound events queued this update (the cloud-jump launch trigger), drained by
    // take_sfx_events(). emit_sfx(0) is a no-op.
    std::vector<std::uint8_t> pending_sfx_;
    void emit_sfx(std::uint8_t id) {
        if (id) pending_sfx_.push_back(id);
    }
};

// The baked world-1 node table (index 0 is an unused sentinel; nodes 1..15).
[[nodiscard]] const MapNode& world1_node(int node);
[[nodiscard]] int world1_node_count() noexcept;  // 15

}  // namespace bumpy

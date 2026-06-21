# World-Map Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the original's world-map screen between the menu and the playfield for world 1 — render `MONDE1.VEC`, move the Bumpy avatar between linked nodes with the arrows, and on fire enter board `node − 1` — retiring the temporary `←/→` board paging.

**Architecture:** A pure, SDL-independent `WorldMap` state machine owns the baked world-1 node graph + positions and the avatar position. A `MapRenderer` composes the MONDE backdrop and the avatar sprite (BUMSPJEU frame `0x21`), reusing a screen-deplane helper factored out of `board_renderer`. `App` gains a `Screen::map` between `menu` and `level`. Game logic stays free of SDL3, refresh rate, and floating point.

**Tech Stack:** C++20, CMake, Catch2 (tests), SDL3 (platform shell only). Existing modules: `decode_vec_resource` (`resources/vec.h`), `decode_sprite_archive`/`SpriteArchive::bytes()` (`resources/menu_resources.h`), `decode_sprite_frame` (`resources/sprite_frame.h`), `IndexedFramebuffer` (`core/indexed_framebuffer.h`).

## Global Constraints

- Game logic (`src/game/*`, `src/video/*`, `src/resources/*`) must not depend on SDL3, the refresh rate, or floating point. SDL3 lives only in `src/platform_sdl3`.
- Never modify the original game files (root-level `*.VEC/*.BIN/*.PAV/*.DEC/*.BUM/*.EXE`); they are read-only inputs.
- C++ tests run with `WORKING_DIRECTORY` = project root, so originals load by bare name (`"MONDE1.VEC"`, `"BUMSPJEU.BIN"`).
- Namespace is `bumpy`. Headers use `#pragma once` and `[[nodiscard]]` on pure accessors, matching the existing code.
- Recovered constants are baked from `BUMPY.UNPACKED.EXE` with the source offset documented in a comment, the way `src/resources/entity_sprites.cpp` does it.
- Build/test command: `cmake --build --preset windows-debug` then run `build/windows-debug/Debug/bumpy_tests.exe` (from the project root). Originals verified with `python tools/assets/manifest.py verify`.

---

### Task 1: `WorldMap` pure state machine + baked world-1 table

**Files:**
- Create: `src/game/world_map.h`
- Create: `src/game/world_map.cpp`
- Test: `tests/cpp/world_map_test.cpp`
- Modify: `CMakeLists.txt` (add `src/game/world_map.cpp` to `bumpy_core`; add `tests/cpp/world_map_test.cpp` to `bumpy_tests`)

**Interfaces:**
- Consumes: `bumpy::MenuInput` from `game/menu.h`.
- Produces:
  - `enum class WorldMapResult { none, select_board, back_to_menu };`
  - `struct WorldMapAction { WorldMapResult result{WorldMapResult::none}; std::size_t board_index{}; };`
  - `struct WorldMapView { int current_node{1}; int avatar_x{}; int avatar_y{}; };`
  - `struct MapNode { std::uint8_t up{}, down{}, left{}, right{}; int x{}; int y{}; };`
  - `class WorldMap` with `WorldMap()`, `void enter() noexcept`, `WorldMapAction update(const MenuInput&) noexcept`, `const WorldMapView& view() const noexcept`, `int current_node() const noexcept`, `std::size_t node_count() const noexcept`.
  - Free: `const MapNode& world1_node(int node);` and `int world1_node_count() noexcept;` (the latter returns 15).

- [ ] **Step 1: Write the failing test**

Create `tests/cpp/world_map_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Add the two `CMakeLists.txt` lines first (so it compiles): in the `add_library(bumpy_core ...)` list add `  src/game/world_map.cpp`, and in `add_executable(bumpy_tests ...)` add `  tests/cpp/world_map_test.cpp`.

Run: `cmake --build --preset windows-debug`
Expected: FAIL — `world_map.h` not found / `bumpy::WorldMap` undefined.

- [ ] **Step 3: Write the header**

Create `src/game/world_map.h`:

```cpp
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
    bool waiting_for_release_{true};
};

// The baked world-1 node table (index 0 is an unused sentinel; nodes 1..15).
[[nodiscard]] const MapNode& world1_node(int node);
[[nodiscard]] int world1_node_count() noexcept;  // 15

}  // namespace bumpy
```

- [ ] **Step 4: Write the implementation**

Create `src/game/world_map.cpp`:

```cpp
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
    enter();
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
}

WorldMapAction WorldMap::update(const MenuInput& input) noexcept {
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
        move_to(n.up);
    } else if (input.down && n.down != 0) {
        move_to(n.down);
    } else if (input.left && n.left != 0) {
        move_to(n.left);
    } else if (input.right && n.right != 0) {
        move_to(n.right);
    } else if (input.confirm) {
        return {WorldMapResult::select_board, static_cast<std::size_t>(view_.current_node - 1)};
    } else if (input.cancel) {
        return {WorldMapResult::back_to_menu, 0};
    }
    return {};
}

}  // namespace bumpy
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe "[world map]" *world*`
Expected: all `world_map_test` cases PASS. (Or run the whole suite: `build/windows-debug/Debug/bumpy_tests.exe` — existing tests still pass; nothing else changed yet.)

- [ ] **Step 6: Commit**

```bash
git add src/game/world_map.h src/game/world_map.cpp tests/cpp/world_map_test.cpp CMakeLists.txt
git commit -m "feat: world-map state machine over the baked world-1 node graph (Stage 3)"
```

---

### Task 2: Factor the screen-deplane helper out of `board_renderer`

**Files:**
- Create: `src/video/screen_image.h`
- Create: `src/video/screen_image.cpp`
- Modify: `src/video/board_renderer.cpp` (replace the private `apply_palette`/`deplane_backdrop` with the shared helper)
- Modify: `CMakeLists.txt` (add `src/video/screen_image.cpp` to `bumpy_core`)

**Interfaces:**
- Consumes: `IndexedFramebuffer` (`core/indexed_framebuffer.h`), `vga_dac_to_rgba_component` (`video/menu_renderer.h`).
- Produces:
  - `inline constexpr int screen_image_width = 320, screen_image_height = 200;`
  - `inline constexpr std::size_t screen_image_plane = 8000, screen_image_pixel_offset = 99, screen_image_palette_offset = 51;`
  - `bool is_screen_image(std::span<const std::uint8_t>) noexcept;`
  - `void apply_screen_image_palette(std::span<const std::uint8_t>, IndexedFramebuffer&);`
  - `void draw_screen_image(std::span<const std::uint8_t>, IndexedFramebuffer&);`

This task is a behavior-preserving refactor; the existing `board_renderer_test` is its regression test.

- [ ] **Step 1: Create the shared header**

Create `src/video/screen_image.h`:

```cpp
#pragma once

#include "core/indexed_framebuffer.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bumpy {

// Screen-format VEC geometry (TITRE/MONDE?): a 99-byte header carrying the 16-colour
// VGA DAC palette (16 RGB triplets ending at the pixel data, so at offset 51), then
// four 8000-byte plane-sequential bit-planes for a 320x200 image.
inline constexpr int screen_image_width = 320;
inline constexpr int screen_image_height = 200;
inline constexpr std::size_t screen_image_plane =
    static_cast<std::size_t>(screen_image_width) * screen_image_height / 8;  // 8000
inline constexpr std::size_t screen_image_pixel_offset = 99;
inline constexpr std::size_t screen_image_palette_offset = screen_image_pixel_offset - 16 * 3;  // 51

// True when `screen` is large enough to be a 320x200 screen-format VEC.
[[nodiscard]] bool is_screen_image(std::span<const std::uint8_t> screen) noexcept;

// Set framebuffer palette entries 0..15 from the screen's embedded DAC palette.
void apply_screen_image_palette(std::span<const std::uint8_t> screen, IndexedFramebuffer& target);

// Deplane the four bit-planes into the framebuffer (full 320x200 overwrite).
void draw_screen_image(std::span<const std::uint8_t> screen, IndexedFramebuffer& target);

}  // namespace bumpy
```

- [ ] **Step 2: Create the implementation**

Create `src/video/screen_image.cpp` (moved verbatim from the anonymous namespace in `board_renderer.cpp`, renamed):

```cpp
#include "video/screen_image.h"

#include "video/menu_renderer.h"  // vga_dac_to_rgba_component

namespace bumpy {

bool is_screen_image(std::span<const std::uint8_t> screen) noexcept {
    return screen.size() >= screen_image_pixel_offset + 4 * screen_image_plane;
}

void apply_screen_image_palette(std::span<const std::uint8_t> screen, IndexedFramebuffer& target) {
    const std::uint8_t* palette = screen.data() + screen_image_palette_offset;
    for (int color = 0; color < 16; ++color) {
        const std::uint8_t* entry = palette + color * 3;
        target.set_palette(static_cast<std::uint8_t>(color),
                           Rgba{vga_dac_to_rgba_component(entry[0]),
                                vga_dac_to_rgba_component(entry[1]),
                                vga_dac_to_rgba_component(entry[2]), 0xff});
    }
}

void draw_screen_image(std::span<const std::uint8_t> screen, IndexedFramebuffer& target) {
    const std::uint8_t* planes = screen.data() + screen_image_pixel_offset;
    for (std::size_t pixel = 0;
         pixel < static_cast<std::size_t>(screen_image_width) * screen_image_height; ++pixel) {
        const std::size_t byte = pixel >> 3U;
        const unsigned shift = 7U - static_cast<unsigned>(pixel & 7U);
        std::uint8_t value = 0;
        for (int plane = 0; plane < 4; ++plane) {
            value = static_cast<std::uint8_t>(
                value | (((planes[plane * screen_image_plane + byte] >> shift) & 1U) << plane));
        }
        target.pixel(static_cast<int>(pixel % screen_image_width),
                     static_cast<int>(pixel / screen_image_width)) = value;
    }
}

}  // namespace bumpy
```

- [ ] **Step 3: Rewrite `board_renderer.cpp` to use the helper**

In `src/video/board_renderer.cpp`: delete the anonymous-namespace block that defines `screen_width`, `screen_height`, `screen_plane`, `pixel_data_offset`, `palette_colors`, `palette_offset`, `apply_palette`, and `deplane_backdrop` (lines ~9–46). Add `#include "video/screen_image.h"` near the other includes. Then change the top of `render_board` from:

```cpp
    if (backdrop_screen.size() < pixel_data_offset + 4 * screen_plane) {
        throw std::runtime_error("backdrop is not a 320x200 screen-format VEC");
    }
    apply_palette(backdrop_screen, target);
    if (draw_map) {
        deplane_backdrop(backdrop_screen, target);  // debug: overlay the world-select map
    } else {
        target.clear(0);  // faithful base-tile pass: every cell cleared to colour index 0
    }
```

to:

```cpp
    if (!is_screen_image(backdrop_screen)) {
        throw std::runtime_error("backdrop is not a 320x200 screen-format VEC");
    }
    apply_screen_image_palette(backdrop_screen, target);
    if (draw_map) {
        draw_screen_image(backdrop_screen, target);  // debug: overlay the world-select map
    } else {
        target.clear(0);  // faithful base-tile pass: every cell cleared to colour index 0
    }
```

(The `<stdexcept>` include in `board_renderer.cpp` stays — `render_board` still throws below.)

- [ ] **Step 4: Add the source to CMake**

In `CMakeLists.txt`, in the `add_library(bumpy_core ...)` list, add after `src/video/menu_renderer.cpp`:

```
  src/video/screen_image.cpp
```

- [ ] **Step 5: Build and run the existing renderer tests to verify no behavior change**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe`
Expected: PASS — all tests, in particular `board_renderer_test` ("rendering level 1 board 0 stamps every grid cell") and `level_resources_test`, are unchanged.

- [ ] **Step 6: Commit**

```bash
git add src/video/screen_image.h src/video/screen_image.cpp src/video/board_renderer.cpp CMakeLists.txt
git commit -m "refactor: extract the screen-format deplane helper for reuse (Stage 3)"
```

---

### Task 3: `MapRenderer` — compose MONDE + avatar

**Files:**
- Create: `src/video/map_renderer.h`
- Create: `src/video/map_renderer.cpp`
- Test: `tests/cpp/map_renderer_test.cpp`
- Modify: `CMakeLists.txt` (add `src/video/map_renderer.cpp` to `bumpy_core`; add `tests/cpp/map_renderer_test.cpp` to `bumpy_tests`)

**Interfaces:**
- Consumes: `WorldMapView` (`game/world_map.h`), `decode_sprite_frame` + `sprite_transparent_index` (`resources/sprite_frame.h`), `apply_screen_image_palette`/`draw_screen_image` (`video/screen_image.h`), `IndexedFramebuffer`.
- Produces:
  - `struct MapRenderStats { bool avatar_drawn{}; };`
  - `inline constexpr int map_avatar_frame = 0x21;`
  - `MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view, std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);`

- [ ] **Step 1: Write the failing test**

Create `tests/cpp/map_renderer_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "game/world_map.h"
#include "resources/menu_resources.h"  // decode_sprite_archive
#include "resources/vec.h"             // decode_vec_resource
#include "video/map_renderer.h"
#include "video/screen_image.h"

// Tests run from the project root, so the originals load by bare name. render_map
// must paint the MONDE1 backdrop and blit the avatar over it at node 1.
TEST_CASE("render_map draws the avatar over the MONDE1 backdrop at node 1") {
    const auto backdrop = bumpy::decode_vec_resource("MONDE1.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto screen = backdrop.decoded_bytes();

    bumpy::WorldMap map;  // node 1, avatar at (32,32)

    bumpy::IndexedFramebuffer with_avatar(320, 200);
    const auto stats = bumpy::render_map(screen, map.view(), bank.bytes(), with_avatar);
    REQUIRE(stats.avatar_drawn);

    // Backdrop-only reference: the avatar must change at least one pixel near node 1.
    bumpy::IndexedFramebuffer backdrop_only(320, 200);
    bumpy::apply_screen_image_palette(screen, backdrop_only);
    bumpy::draw_screen_image(screen, backdrop_only);

    int differing = 0;
    for (int y = 24; y < 60; ++y) {
        for (int x = 24; x < 60; ++x) {
            if (with_avatar.pixel(x, y) != backdrop_only.pixel(x, y)) {
                ++differing;
            }
        }
    }
    REQUIRE(differing > 0);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Add the two `CMakeLists.txt` lines (`src/video/map_renderer.cpp` to `bumpy_core`, `tests/cpp/map_renderer_test.cpp` to `bumpy_tests`), then:
Run: `cmake --build --preset windows-debug`
Expected: FAIL — `map_renderer.h` not found / `bumpy::render_map` undefined.

- [ ] **Step 3: Write the header**

Create `src/video/map_renderer.h`:

```cpp
#pragma once

#include "core/indexed_framebuffer.h"
#include "game/world_map.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bumpy {

struct MapRenderStats {
    bool avatar_drawn{};
};

// The Bumpy avatar's sprite frame in the BUMSPJEU.BIN bank, recovered from the map
// avatar draw FUN_1000_1cb2 (blits frame DAT_824a, which FUN_1000_3852 sets to 0x21).
inline constexpr int map_avatar_frame = 0x21;

// Compose the world-map screen: deplane the MONDE backdrop with its embedded palette,
// then blit the avatar (BUMSPJEU frame 0x21) at the current node's avatar position
// (colour index 0 transparent). monde_screen is a decoded 320x200 screen-format VEC;
// sprite_bank is the whole BUMSPJEU.BIN. If the avatar frame fails to decode it is
// skipped (avatar_drawn stays false) rather than throwing.
MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view,
                          std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);

}  // namespace bumpy
```

- [ ] **Step 4: Write the implementation**

Create `src/video/map_renderer.cpp`:

```cpp
#include "video/map_renderer.h"

#include "resources/sprite_frame.h"
#include "video/menu_renderer.h"  // MenuImage
#include "video/screen_image.h"

#include <exception>

namespace bumpy {

MapRenderStats render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView& view,
                          std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target) {
    apply_screen_image_palette(monde_screen, target);
    draw_screen_image(monde_screen, target);

    MapRenderStats stats;
    MenuImage avatar;
    try {
        avatar = decode_sprite_frame(sprite_bank, map_avatar_frame);
    } catch (const std::exception&) {
        return stats;  // avatar frame unavailable -> map renders without it
    }

    for (int py = 0; py < avatar.height; ++py) {
        const int ty = view.avatar_y + py;
        if (ty < 0 || ty >= target.height()) {
            continue;
        }
        for (int px = 0; px < avatar.width; ++px) {
            const int tx = view.avatar_x + px;
            if (tx < 0 || tx >= target.width()) {
                continue;
            }
            const auto color = avatar.pixels[static_cast<std::size_t>(py) * avatar.width + px];
            if (color != sprite_transparent_index) {
                target.pixel(tx, ty) = color;
            }
        }
    }
    stats.avatar_drawn = true;
    return stats;
}

}  // namespace bumpy
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe`
Expected: PASS — `map_renderer_test` plus all prior tests. (If `differing == 0`, the avatar's opaque pixels fall outside the 24..60 window; widen the window in the test and re-run. Frame 0x21 is ~16–24px around the node.)

- [ ] **Step 6: Commit**

```bash
git add src/video/map_renderer.h src/video/map_renderer.cpp tests/cpp/map_renderer_test.cpp CMakeLists.txt
git commit -m "feat: render the world map (MONDE backdrop + Bumpy avatar frame 0x21) (Stage 3)"
```

---

### Task 4: Wire `Screen::map` into `App` and retire board paging

**Files:**
- Modify: `src/game/app.h` (add `Screen::map`, a `WorldMap` member + accessor; update the class comment)
- Modify: `src/game/app.cpp` (menu→map→level/menu transitions; remove `←/→` paging)
- Modify: `tests/cpp/app_test.cpp` (rewrite for the new flow)

**Interfaces:**
- Consumes: `WorldMap`, `WorldMapResult`, `WorldMapAction` (`game/world_map.h`); `Menu`/`MenuInput`/`MenuAction` (`game/menu.h`).
- Produces (additions to `App`): `enum class Screen { menu, map, level };`; `const WorldMap& world_map() const noexcept;`. `update`, `screen`, `menu`, `board_index`, `board_count` keep their existing signatures.

- [ ] **Step 1: Update the failing test**

Replace the entire contents of `tests/cpp/app_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "game/app.h"

namespace {

// Drive the App from the menu to the level the way a player would: confirm "start"
// (menu -> world map), release, then fire on node 1 (map -> level board 0), release.
void enter_level(bumpy::App& app) {
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);  // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::level);
    REQUIRE(app.board_index() == 0);
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);  // release
}

}  // namespace

TEST_CASE("app starts on the menu at board zero") {
    const bumpy::App app(15);

    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.board_index() == 0);
    REQUIRE(app.board_count() == 15);
}

TEST_CASE("confirming the top menu item enters the world map on node 1") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);
    REQUIRE(app.world_map().current_node() == 1);
}

TEST_CASE("firing on a map node enters that node's board") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);    // node 2
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // fire
    REQUIRE(app.screen() == bumpy::Screen::level);
    REQUIRE(app.board_index() == 1);  // node 2 -> board 1
}

TEST_CASE("cancel on the world map returns to the menu") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
}

TEST_CASE("re-entering the map resets the avatar to node 1") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release
    REQUIRE(app.update(bumpy::MenuInput{.right = true}) == bumpy::AppOutcome::running);    // node 2
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);   // -> menu
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.world_map().current_node() == 1);
}

TEST_CASE("cancel on the level screen returns to the menu") {
    bumpy::App app(15);
    enter_level(app);

    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
}

TEST_CASE("cancel on the menu quits") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("quitting from the menu's exit row propagates") {
    bumpy::App app(15);

    for (int row = 0; row < 3; ++row) {
        REQUIRE(app.update(bumpy::MenuInput{.down = true}) == bumpy::AppOutcome::running);
        REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    }
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("a held confirm does not bounce menu -> map -> level") {
    bumpy::App app(15);

    // First confirm edge: menu -> map.
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);

    // Still holding confirm: must not select a board until the key is released.
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::map);

    // Release, then a fresh confirm edge enters the level.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::level);
}

TEST_CASE("a held cancel does not bounce map -> menu -> quit") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // -> map
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);                 // release

    // First cancel edge: map -> menu.
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);

    // Still holding cancel: must not quit until released.
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::running);

    // Release, then a fresh cancel edge quits from the menu.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.cancel = true}) == bumpy::AppOutcome::quit);
}

TEST_CASE("left/right do nothing on the menu screen") {
    bumpy::App app(15);

    REQUIRE(app.update(bumpy::MenuInput{.left = true, .right = true}) ==
            bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.board_index() == 0);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset windows-debug`
Expected: FAIL — `Screen::map` and `app.world_map()` do not exist yet (compile error).

- [ ] **Step 3: Update `app.h`**

In `src/game/app.h`: add the include and the new screen value + member. Replace:

```cpp
#include "game/menu.h"

#include <cstddef>
```

with:

```cpp
#include "game/menu.h"
#include "game/world_map.h"

#include <cstddef>
```

Replace the `Screen` enum:

```cpp
enum class Screen {
    menu,
    level,
};
```

with:

```cpp
enum class Screen {
    menu,
    map,
    level,
};
```

Update the class comment's transition list to:

```cpp
//   menu  --confirm "start"--> map (world map, node 1)
//   menu  --cancel-----------> quit
//   map   --fire (confirm)----> level (board = selected node - 1)
//   map   --cancel-----------> menu
//   level --cancel-----------> menu
```

Add the accessor next to the other `[[nodiscard]]` getters:

```cpp
    [[nodiscard]] const WorldMap& world_map() const noexcept { return world_map_; }
```

Add the member next to `Menu menu_;`:

```cpp
    WorldMap world_map_;
```

- [ ] **Step 4: Rewrite `App::update` in `app.cpp`**

Replace the whole body of `src/game/app.cpp` with:

```cpp
#include "game/app.h"

namespace bumpy {

App::App(std::size_t board_count) noexcept : board_count_(board_count) {}

AppOutcome App::update(const MenuInput& input) noexcept {
    // App owns cancel edge detection on every screen so a cancel that causes a
    // transition (e.g. map -> menu) cannot bounce into the next screen's cancel.
    // confirm is owned per-screen: Menu debounces it on the menu, WorldMap on the
    // map. left/right only matter as a transition key... never now (paging retired).
    const bool app_key = input.cancel;
    const bool app_edge = app_key && !waiting_for_release_;
    waiting_for_release_ = app_key;

    if (screen_ == Screen::menu) {
        switch (menu_.update(input)) {
        case MenuAction::start_first_level:
            world_map_.enter();  // reset to node 1; require key release before acting
            screen_ = Screen::map;
            return AppOutcome::running;
        case MenuAction::quit:
            return AppOutcome::quit;
        case MenuAction::none:
            break;
        }
        if (app_edge && input.cancel) {
            return AppOutcome::quit;  // Escape from the menu exits the game
        }
        return AppOutcome::running;
    }

    if (screen_ == Screen::map) {
        const auto action = world_map_.update(input);
        switch (action.result) {
        case WorldMapResult::select_board:
            board_index_ = action.board_index;  // map node N -> board N-1
            screen_ = Screen::level;
            return AppOutcome::running;
        case WorldMapResult::back_to_menu:
            screen_ = Screen::menu;
            return AppOutcome::running;
        case WorldMapResult::none:
            break;
        }
        return AppOutcome::running;
    }

    // Screen::level (display only; cancel returns to the menu)
    if (app_edge && input.cancel) {
        screen_ = Screen::menu;
        return AppOutcome::running;
    }
    return AppOutcome::running;
}

}  // namespace bumpy
```

(The `waiting_for_release_` member already exists in `app.h`. The `board_count_`/`board_index_` members stay; `board_count()` remains an informational accessor. `<cstddef>` is already included via `app.h`.)

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe`
Expected: PASS — all of `app_test` (new flow) plus every prior test.

- [ ] **Step 6: Commit**

```bash
git add src/game/app.h src/game/app.cpp tests/cpp/app_test.cpp
git commit -m "feat: route menu -> world map -> level in App, retiring board paging (Stage 3)"
```

---

### Task 5: SDL render arm + `--render-map` CLI flag

**Files:**
- Modify: `src/platform_sdl3/sdl_app.cpp` (render `Screen::map`)
- Modify: `src/app/main.cpp` (add the `--render-map` headless flag)

**Interfaces:**
- Consumes: `App::screen()`/`App::world_map()`, `render_map` (`video/map_renderer.h`), the already-loaded `backdrop_screen` (MONDE1 bytes) and `sprite_bank` (BUMSPJEU bytes) `SdlApp::run` already receives.
- Produces: no new public types. `bumpy_port.exe --render-map <world> <MONDE.VEC> <out.bmp>` dumps the composed world-map screen to a BMP.

- [ ] **Step 1: Add the `Screen::map` render arm in `sdl_app.cpp`**

In `src/platform_sdl3/sdl_app.cpp`, add the include near the top:

```cpp
#include "video/map_renderer.h"
```

In `SdlApp::run`, replace the render branch:

```cpp
        if (app.screen() == Screen::menu) {
            menu_renderer.render(app.menu().view(), frame);
        } else {
            render_board(level, app.board_index(), backdrop_screen, frame);
            draw_bum_entities(level.bum_entities(app.board_index()), sprite_bank, frame);
        }
```

with:

```cpp
        if (app.screen() == Screen::menu) {
            menu_renderer.render(app.menu().view(), frame);
        } else if (app.screen() == Screen::map) {
            render_map(backdrop_screen, app.world_map().view(), sprite_bank, frame);
        } else {
            render_board(level, app.board_index(), backdrop_screen, frame);
            draw_bum_entities(level.bum_entities(app.board_index()), sprite_bank, frame);
        }
```

- [ ] **Step 2: Add the `render_map_to_bmp` helper in `main.cpp`**

In `src/app/main.cpp`, add the include near the other video includes:

```cpp
#include "video/map_renderer.h"
```

Add this function in the anonymous namespace, after `render_board_to_bmp` (before `run_sdl_menu`):

```cpp
// Compose the world-map screen (MONDE backdrop + the Bumpy avatar at node 1) and
// dump it to a BMP for by-eye comparison with the original world-select capture.
int render_map_to_bmp(const std::filesystem::path& asset_root, const std::filesystem::path& monde_path,
                      const std::filesystem::path& out_path) {
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::WorldMap map;  // node 1
    bumpy::IndexedFramebuffer frame(320, 200);
    const auto stats = bumpy::render_map(backdrop.decoded_bytes(), map.view(), bank.bytes(), frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << " (avatar "
              << (stats.avatar_drawn ? "drawn" : "skipped") << " at node "
              << map.current_node() << ")\n";
    return 0;
}
```

Add the include for `WorldMap` near the other game includes:

```cpp
#include "game/world_map.h"
```

(`map_renderer.h` already pulls in `world_map.h`, but include it explicitly since `main.cpp` names `bumpy::WorldMap` directly.)

- [ ] **Step 3: Add the CLI dispatch in `main`**

In `src/app/main.cpp`'s `main`, add this branch alongside the other `--render-*` checks (e.g. right after the `--render-screen` branch):

```cpp
        if (argc == 5 && std::string_view(argv[1]) == "--render-map") {
            // --render-map <world> <MONDE.VEC> <out.bmp>
            // world is currently informational (world 1 only); MONDE.VEC supplies the
            // backdrop + palette, and the avatar is drawn at node 1.
            return render_map_to_bmp(asset_root, argv[3], argv[4]);
        }
```

- [ ] **Step 4: Build and dump the world-map BMP**

Run:
```
cmake --build --preset windows-debug
build/windows-debug/Debug/bumpy_port.exe --render-map 1 MONDE1.VEC analysis/generated/world_map_1.bmp
```
Expected: prints `wrote analysis/generated/world_map_1.bmp (avatar drawn at node 1)`.

- [ ] **Step 5: Verify by eye and confirm originals are clean**

Open `analysis/generated/world_map_1.bmp` and compare with `screenshots/bumpy_001.png`: the world-1 map art and 16-colour palette match, and the Bumpy avatar sits on the top-left node (node 1, ≈(32,32)). If the avatar is visibly off the ring, adjust its draw position: the node table holds the ring centre, so nudging the blit in `render_map` (e.g. `view.avatar_x - dx`, `view.avatar_y - dy` by a few px to centre the sprite) and re-dumping is the by-eye tuning the design calls for. Record the chosen offset in a comment.

Then confirm the originals are untouched:
Run: `python tools/assets/manifest.py verify`
Expected: no missing/changed originals.

- [ ] **Step 6: Full build, full test run, and a live smoke check**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe`
Expected: PASS — all tests.

Launch `build/windows-debug/Debug/bumpy_port.exe`: menu → confirm "start" shows the world map → arrows move Bumpy between linked nodes → fire enters a board → Escape returns to the menu. (This is the live equivalent of the recovered `menu → world map → playfield` flow.)

- [ ] **Step 7: Commit**

```bash
git add src/platform_sdl3/sdl_app.cpp src/app/main.cpp
git commit -m "feat: show the world map in-window and add --render-map (Stage 3)"
```

---

## Post-implementation

After Task 5, update `PROJECT_STATUS.md`: move the world-map screen from "Next step" to a completed "Stage 3 world-map screen" entry (menu → map → board flow, `WorldMap` + `MapRenderer`, `--render-map` flag, board paging retired), and set the new next step to the in-level gameplay palette / physics. Commit that as `docs: record the world-map screen (Stage 3)`. (This documentation step is intentionally outside the task list so it captures the final, verified state.)

## Out of scope (tracked in the design's follow-ups)

4px-per-step avatar slide animation; the 7-digit score + lives HUD; completed-node markers (frame `0x1da`); worlds 2–9; the in-level gameplay palette and physics/collision/win-loss.

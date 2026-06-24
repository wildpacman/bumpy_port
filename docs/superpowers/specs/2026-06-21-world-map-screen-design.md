# World-map screen — design

Date: 2026-06-21. Status: approved for planning.

## Goal

Add the missing **world-map screen** between the menu and the playfield, so the
port follows the original's flow **menu → world map → playfield** instead of
jumping menu → playfield. This is the milestone identified in `PROJECT_STATUS.md`
("Next step") and fully recovered in `analysis/specs/screen-flow.md`.

Scope for this slice (decided with the project owner):

- **World 1 only.** Matches the current single-level wiring and the project's
  "first level playable" definition of done. Worlds 2–9 generalize later, once
  win/loss advances worlds.
- **Avatar + navigation.** Render `MONDE1.VEC` (already works), draw the Bumpy
  avatar at the current node, move it between linked nodes with the arrow keys
  per the recovered graph, and on fire enter board `node − 1`. The score/lives
  HUD overlays are a later pass.
- **Retire `←/→` board paging.** Node selection on the map replaces the temporary
  `←/→` paging on the level screen. The headless `--render-board` flags stay.

Non-goals for this slice: the 7-digit score / lives HUD, completed-node markers
(frame `0x1da`), the original's 4px-per-step avatar slide animation, the EGA
`0x23` palette patch, and worlds 2–9.

## Recovered behavior (evidence)

All addresses are `segment:offset` in `analysis/generated/decomp/all_functions.c`;
data-segment offsets resolve in `BUMPY.UNPACKED.EXE` at file offset
`0x11440 + DS_offset` (data segment `0x103b`). Full trace in
`analysis/specs/screen-flow.md`.

- **Flow** (`FUN_1000_0c18`): after "start" on the menu, the loop runs
  `FUN_1000_2d14` (load `D{world}.PAV/DEC/BUM`, zero every node's state byte) then
  the world-map screen `FUN_1000_3852`; the selected node becomes the board index
  (`DAT_7310 = DAT_854e − 1`). Escape on the map sets `DAT_928d = -1` → back to the
  menu.
- **Avatar** (`FUN_1000_1cb2`, called per redraw from `FUN_1000_3c26`): blits a
  single sprite frame whose index is `DAT_824a` at `(DAT_9290, DAT_9292)`. The map
  sets `DAT_824a = 0x21` (`FUN_1000_3852` @ `1000:4290`), so **the avatar is
  BUMSPJEU frame `0x21`** — the same decode/blit path the entity sprites already
  use (`decode_sprite_frame(bank, idx)`).
- **Navigation** (`FUN_1000_3a88` + `3ab2`/`3b0f`/`3b6c`/`3bc9`): the current node
  record pointer is `base + node*9`; each arrow reads a neighbour byte and, if
  non-zero, sets `DAT_854e = neighbour` and slides the avatar `dist>>2` steps of
  ±4px along the axis, redrawing each step. Input bits `DAT_8244`:
  `1`=up, `2`=down, `4`=left, `8`=right, `0x10`=fire (`FUN_1000_3cf7` → selected),
  else Escape.
- **Selection** (`FUN_1000_3cf7`): fire returns "selected" and the map loop exits;
  the board index is `node − 1`. World 1's 15 nodes map to D1's 15 boards.

### World-1 node table (extracted bytes, confirmed)

Graph base `DS:0x09e6` (file `0x11E26`); node `N` (1-based) record at `base + N*9`;
node 0 slot is zero padding; an `0xff` first byte terminates. Each 9-byte record:

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| Field | state | up nbr | up dist | down nbr | down dist | left nbr | left dist | right nbr | right dist |

Distances are pixels; the slide takes `dist>>2` steps of 4px. Spot-checked:
node 1 = `00 00 00 00 00 00 00 02 50` (right→2, 80px); node 15 = `00 0b 30 00 00 0e
50 00 00` (up→11 48px, left→14 80px) — both match `screen-flow.md`.

Positions base `DS:0x0a80` (file `0x11EC0`); `(x, y)` little-endian word pairs
indexed by `node − 1`, 15 entries. node 1 = (32, 32) … node 15 = (272, 176).

The implementation **bakes both tables** into the map module (constants extracted
from `BUMPY.UNPACKED.EXE` at the offsets above), the same way
`src/resources/entity_sprites` bakes its recovered descriptor tables. The graph is
inside the executable, not a separate asset, so there is no runtime file to read.

## Architecture (Approach A)

Three small units with clear boundaries, mirroring the existing `menu`/`board`
split and keeping game logic independent of SDL3.

### `src/game/world_map.{h,cpp}` — pure state machine

- Owns: the baked world-1 graph (15 × 9-byte records) + positions (15 × (x,y)),
  the current node (1-based, starts at 1), and the avatar pixel position.
- `WorldMapAction update(const MenuInput&)` returns one of:
  `none`, `select_board(std::size_t board_index)`, `back_to_menu`.
  - arrows → move to the linked neighbour if its neighbour byte ≠ 0, else no-op.
    This slice **snaps** the avatar to the neighbour's position (the original's
    4px slide is noted as optional polish; topology and selection are identical).
  - confirm (fire) → `select_board(current_node − 1)`.
  - cancel → `back_to_menu`.
  - Debounces its arrow keys with the same `waiting_for_release_` pattern `Menu`
    uses for up/down, so a held arrow steps one node per press.
    > **Superseded (2026-06-24):** arrow debounce was removed to match the original —
    > the map loop (`FUN_1000_3852`) re-polls held keys with no debounce, so holding a
    > direction now walks node to node continuously; only fire/cancel stay
    > release-guarded. See `analysis/specs/screen-flow.md` and `PROJECT_STATUS.md`.
- `const WorldMapView& view()` exposes `current_node` and avatar `(x, y)` for the
  renderer. No SDL, no file I/O, no floating point.

### `src/video/map_renderer.{h,cpp}` — composition

- `render_map(std::span<const std::uint8_t> monde_screen, const WorldMapView&,
  std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer&)`:
  1. deplane the 320×200 MONDE screen + apply its embedded palette;
  2. blit the avatar = `decode_sprite_frame(sprite_bank, 0x21)` at the avatar
     position (colour index 0 transparent).
- The screen-deplane + palette helper currently lives in an anonymous namespace
  in `board_renderer.cpp` (`apply_palette` / `deplane_backdrop`). Factor it into a
  shared header (e.g. `src/video/screen_image.{h,cpp}`) and have both
  `board_renderer` and `map_renderer` use it, rather than duplicating it.
- Node markers (frame `0x1da`) are **deferred**: `FUN_1000_3c4f` draws them only
  for nodes whose state byte ≠ 0, and `FUN_1000_2d14` zeroes all state bytes at
  load, so no markers draw for a fresh world. The visible rings are baked into the
  MONDE art.

### `src/game/app.{h,cpp}` — add `Screen::map`

- New transitions (replacing the direct menu→level jump):
  - `menu --confirm "start"--> map`
  - `map --fire (confirm)--> level` with `board_index = selected node − 1`
  - `map --cancel--> menu`
  - `level --cancel--> menu` (unchanged)
- The `←/→` board paging on `Screen::level` is **removed**.
- `App` owns a `WorldMap` member and delegates map updates to it. `App` keeps its
  existing edge detection for `cancel`; it adds edge detection for `confirm` on
  the map screen (reusing `waiting_for_release_`) so holding fire across map→level
  cannot double-trigger. The `WorldMap` debounces its own arrows.

## Data flow & wiring

Per-frame, the shell still makes one `app.update(input)` call, then renders by
`app.screen()`:

- `main.cpp` already loads level 1, `MONDE1.VEC`, and the `BUMSPJEU` bank up front
  and keeps them alive for `run()`. `MapRenderer` needs exactly the already-passed
  `backdrop_screen` (MONDE1 decoded bytes) and `sprite_bank` — **no new asset
  loading**. `App` is still constructed with the level's board count; it also
  builds its `WorldMap` from the baked world-1 table.
- `sdl_app.cpp`'s render switch gains a `Screen::map` arm:
  `render_map(backdrop_screen, app.world_map().view(), sprite_bank, frame)`. The
  `Screen::level` and `Screen::menu` arms are unchanged.
- Input mapping in `sdl_app.cpp` is already complete (arrows; Enter/Space →
  confirm/fire; Escape → cancel). No new key wiring.

## Testing

- `tests/cpp/world_map_test.cpp` — unit tests on the pure state machine: starts on
  node 1; from node 1 only `right` moves (to node 2), up/down/left are no-ops; a
  multi-hop path (e.g. 1→2→9→8) lands on the expected nodes; fire returns
  `select_board(node − 1)`; cancel returns `back_to_menu`; a held arrow steps one
  node per press (edge detection). The baked table is asserted against the
  `screen-flow.md` world-1 listing (positions + links).
- `tests/cpp/app_test.cpp` — update the existing 8 tests: `menu confirm → map`
  (not level); `map fire → level` at the selected board; `map cancel → menu`;
  `level cancel → menu`. Remove the `←/→` paging tests.
- `tests/cpp/map_renderer_test.cpp` — headless smoke test: render into a
  framebuffer and assert the avatar pixels land near node 1's position. A
  `--render-map <world> <MONDE.VEC> <out.bmp>` CLI flag (mirroring `--render-board`)
  dumps the composed map for by-eye comparison with `screenshots/bumpy_001.png`.
- `board_renderer` / `level_resources` tests are unaffected by the factored
  screen-image helper (same behavior, relocated).

## Edge cases & error handling

- A zero-neighbour arrow is a no-op — no move, no crash.
- Fire on any open node is always valid: all 15 world-1 nodes map to D1's 15
  boards (`bum_board_count == 15`).
- The avatar's exact pixel offset vs. the baked ring centers is tuned **by eye**
  against `screenshots/bumpy_001.png`, the same validation the board renderer used.
  The node positions are the ring centers; frame `0x21`'s own pixels determine the
  visual centering.
- `decode_sprite_frame(bank, 0x21)` is expected to succeed (uncompressed bank); if
  it ever throws, the avatar is skipped rather than crashing the frame (matching
  `draw_bum_entities`' defensive skip).

## Verification gate (before commit)

- `cmake --build --preset windows-debug` clean;
- all C++ tests pass (existing + new `world_map`/`map_renderer`, updated `app`);
- originals verify clean (`python tools/assets/manifest.py verify`);
- `--render-map 1 MONDE1.VEC out.bmp` matches the world-1 capture by eye, and the
  live window shows menu → (start) → map with a movable avatar → (fire) → board →
  (Escape) → menu.

## Out of scope (follow-ups)

- 4px-per-step avatar slide animation (atomic snap is used here).
- 7-digit score (`FUN_1000_0816`) and lives HUD on the map.
- Completed-node markers (frame `0x1da`, `FUN_1000_3c4f`).
- Worlds 2–9 (extract `0x10c8[world]` / `0x10ec[world]` graphs+positions; load
  MONDE/level on demand) — unlocked once win/loss advances worlds.
- In-level gameplay palette (`0x23` EGA patch) and physics/collision/win-loss.

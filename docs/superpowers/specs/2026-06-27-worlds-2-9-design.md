# Worlds 2–9 — design

Date: 2026-06-27. Status: approved (brainstorm), pending spec review.

## Goal

Make the port play all **9 worlds** in the original's faithful **linear** order.
Today world 1 is fully playable (menu → world map → playfield → win/lose, with a
persistent run), but three things hardcode world 1:

- `src/game/world_map` bakes a single `kWorld1` node graph + positions array.
- `src/app/main.cpp` / `src/platform_sdl3/sdl_app` load `D1` + `MONDE1.VEC` **once**
  and hold them by reference/span for the whole loop.
- `src/game/app` `finish_level` → all boards cleared → "world complete" **stubs to
  the menu** instead of advancing to the next world.

Definition of done: starting a game plays world 1; clearing every node in a world
advances to the next world (new map art, new boards, node-completion reset), through
world 9; clearing world 9 returns to the menu (the outro is deferred). A dev
`--start-world N` shortcut and world-aware headless render tools let any world be
reached/verified without clearing the ones before it.

## Recovered mechanic (from `analysis/specs/screen-flow.md`)

Progression is **linear**, driven by the main loop `FUN_1000_0c18` and the
world-complete check `FUN_1000_3e8a`:

- The world number is `DAT_203b_79b2` (1..9). The current map node is
  `DAT_203b_854e` (1-based).
- **Win a board** (`FUN_1000_1e3d`): marks byte 0 of the current node's 9-byte graph
  record (`*_DAT_9baa = 1`) and returns to the map; the node shows the completed
  marker (frame `0x1da`).
- **All nodes cleared** (`FUN_1000_3e8a`): ANDs byte 0 of every node record; if all
  are 1 → `DAT_79b2++`. At world `10` it plays the outro `FUN_1000_3ed4` (deferred);
  otherwise `FUN_1000_2d14` loads the next world's `MONDE`/`D`-files and **clears all
  node bytes** (every node re-opens), and the avatar returns to the start node.
- **Lose a life** (`FUN_1000_22fc`): the node is not marked (replayable); out of lives
  → game over → menu. (Unchanged — already ported.)

Per-world data (far pointers, indexed by world):

- **Node graph** at `0x10c8[world]`: array of **9-byte records** (1-based node index),
  terminated by a record whose byte 0 is `0xff`. Bytes: `{state, up_nbr, up_dist,
  down_nbr, down_dist, left_nbr, left_dist, right_nbr, right_dist}`.
- **Node positions** at `0x10ec[world]`: `(x,y)` word pairs indexed by `node-1`.
- World 1 already resolved to `DS:0x09e6` (graph) / `DS:0x0a80` (positions); the baked
  `kWorld1` is the anchor for the extractor.
- Avatar start: `FUN_1000_2d14` sets `(DAT_791c, DAT_791e)` = `(0x1f,0x1f)` for most
  worlds, `(0x6f,0x1f)` for worlds **2 and 5** — i.e. the start node's pixel position.
  Node 1 is always the start node; its position comes from `0x10ec[world]`, so the
  positions table is authoritative (the constant is a cross-check, not a separate input).

Per-world map palette (the `0x33` DAC palette embedded in each `MONDE{n}.VEC`) and
per-board playfield palette (the `D{n}.DEC` board header) already render correctly via
the existing code paths — no new palette work.

## Architecture — Approach A (lazy reload, shell-owned)

The baked node graph is compiled-in; only the **disk** resources change per world:
one world's `LevelResources` (`D{n}.PAV/DEC/BUM`) and the decoded `MONDE{n}.VEC`
backdrop. The world-independent `BUMSPJEU.BIN` sprite bank and `DDFNT2.CAR` font stay
loaded once.

`App` stays SDL/disk-free: it owns the world **number** + the baked `WorldMap(world)`
and only *requests* a world load and *receives* the resulting board count. The SDL
shell owns disk I/O and reloads when `App` requests a new world; the reload happens at
a screen change, hidden under the edge-to-centre darken.

```
menu "start"
  -> App::reset_run()  : world = start_world; request_world(start_world)
  -> shell: load WorldResources(start_world); App::enter_world(n, board_count)
  -> map -> play boards ...
  -> clear all nodes in world n:
       n < 9 -> App::request_world(n+1) -> shell reload -> enter_world -> map
       n == 9 -> game complete (stub) -> menu (reset_run)
```

### Components

1. **`tools/re/dump_world_graphs.py` → `src/game/world_graphs.{h,gen.cpp}`**.
   Read `BUMPY.UNPACKED.EXE`; follow `0x10c8[world]` / `0x10ec[world]` for worlds 1–9;
   walk each world's 9-byte records to the `0xff` terminator; emit baked `MapNode`
   tables + per-world node count. Hand-written `world_graphs.h` declares the accessors;
   the generated `world_graphs.gen.cpp` holds the tables — same split as the existing
   `src/game/object_anim.{h,gen.cpp}`. **Generated world 1 must equal the existing
   `kWorld1` byte-for-byte** (asserted by a test). The resolved addresses are recorded in
   a header comment. The generated `.cpp` is committed (it is source, not a build
   artifact — same pattern as the other `*.gen.cpp`). The start node is always node 1
   (`DAT_854e = 1`); only its pixel position varies per world (positions table), so no
   per-world start-node accessor is needed.

2. **`src/game/world_map`** — parameterize by world. Replace the single `kWorld1` /
   `world1_node*` with `world_node(world, node)` and `world_node_count(world)` backed by
   `world_graphs`. `WorldMap` gains a `world_` field + `load_world(int world)`;
   `WorldMap()` defaults to world 1; `enter()` / `move_to` use the current world's start
   node (always node 1) + table. `node_count()` returns the current world's count. The
   cloud-jump table (`kJump`) is world-independent and unchanged.

3. **`src/game/app`** — world-aware run + linear advance.
   - Add `world_` (1-based), `start_world_` (default 1, dev override),
     `pending_world_` (0 = none).
   - `reset_run()` → `world_ = start_world_`; request that world (instead of going
     straight to the map).
   - `finish_level(won, ...)`: mark node cleared; if `all_boards_cleared()`:
     `world_ < 9` → `request_world(world_+1)`; `world_ == 9` → game complete → menu
     (`reset_run`). Not all cleared → back to the current world's map (unchanged).
   - Handshake: `request_world(n)` sets `pending_world_ = n` (App freezes until loaded);
     `enter_world(n, board_count)` rebuilds `WorldMap` for world n, resizes `cleared_`
     to the world's node count, sets `board_count_`, clears `pending_world_`, screen → map.
   - `pending_world()` accessor for the shell.

4. **`src/platform_sdl3/sdl_app` + new `WorldResources`**.
   - `WorldResources` owns one world's `LevelResources level` + decoded `MONDE{n}.VEC`
     bytes; exposes `board_count()` and the backdrop span. `WorldResources::load(root, n)`.
   - `SdlApp::run` holds the current `WorldResources` (mutable) + the world-independent
     `sprite_bank` / `font`; `run()` takes the `asset_root` (to reload) and the initial
     `WorldResources` instead of a fixed `level`+`backdrop`.
   - Each frame, if `app.pending_world() != 0`: load that world's `WorldResources`
     (on failure: warn, drop back to the menu via `reset_run` to world 1 — never crash),
     then `app.enter_world(n, current.board_count())`. The reload lands on the screen
     change so the darken covers it.
   - Preserve the node→board guard: a selected board index ≥ `bum_board_count` (e.g.
     D7's 12 boards vs 15 nodes) → `leave_level()` (no entity data), as today.

5. **`src/app/main.cpp`** — wiring + dev access.
   - Construct `App` with a start world; load that world's `WorldResources` initially.
   - `--start-world N` (1..9) sets the dev start world.
   - Make the existing `<world>` arg on `--render-map` real (load `MONDE{world}` +
     world n's graph; avatar on the world's start node). Add a world arg to
     `--render-play` so each world's boards are verifiable headlessly. `--render-jump`
     similarly world-aware (optional, low priority).

## Data flow

`App` is the single source of truth for `world_` and `pending_world_`. The shell is a
pure consumer: it loads what `App` asks for and reports back the board count. No world
state lives in the shell except the cached `WorldResources` it last loaded.

## Error handling

- Missing/altered `MONDE{n}.VEC` or `D{n}.*`: the asset-manifest check already warns at
  launch. A failed `WorldResources::load` in the run loop is caught → warn + `reset_run`
  to world 1 menu, never a crash.
- Node count vs board count mismatch (D7: 12 boards, 15 nodes): node→board guard
  (`board_index >= bum_board_count` → `leave_level`) preserved; a node with no board is
  simply non-completable (matches the original's behavior for short level files).
- Extractor: if `0x10c8`/`0x10ec` do not resolve to sane records (terminator found,
  positions in 0..319/0..199, node count ≥ 1), the tool fails loudly rather than emit
  garbage; fall back to per-world hand-extraction (each graph is tiny).

## Testing

- **`world_graphs_test`** (new): generated world-1 table == the historical `kWorld1`
  constants (regression anchor); every world has node_count ≥ 1, a valid start node, and
  all positions within 320×200.
- **`world_map_test`** (parameterized per world 1..9): node count matches the graph;
  links are **symmetric** (if A.right == B then B.left == A, etc.); navigation from the
  start node via BFS reaches **every** node (no orphaned nodes); positions in-bounds.
- **`app_test`**: all-nodes-cleared in world n<9 → `pending_world() == n+1`;
  `enter_world` resizes `cleared_` and resets the map to the start node; world 9 cleared
  → menu + run reset; `--start-world` override lands the first game on world N.
- **By eye**: `--render-map n MONDEn.VEC out.bmp` for n = 2..9 shows the avatar on each
  world's start node over that world's art; spot-play a board in a couple of later worlds
  via `--render-play`.
- Originals verify clean (`python tools/assets/manifest.py verify`) before/after; full
  C++ suite passes.

## Out of scope (this slice)

- The world-10 outro (`FUN_1000_3ed4` / `DESSFIN.VEC`) — stubbed to the menu.
- The password screen (`FUN_1000_11eb`) as a world-resume path — separate menu work.
- The `0x6e6[world]` EGA attribute-palette patch (the port uses the DAC palette; no
  visible difference, same as world 1).

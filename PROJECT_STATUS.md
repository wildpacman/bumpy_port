# Bumpy Port — Project Status

Source of truth for new sessions. Last updated: 2026-06-22.

## Goal

A **playable** native Windows 11 port of *Bumpy's Arcade Fantasy* in C++/SDL3
that reads the original game's resources directly. Faithful to the original's
look, timing, and behavior — not a bit-for-bit preservation project.

First slice (definition of done): the original menu and the first level are
playable from launch through win/loss and back to the menu, reading the supplied
original files.

## Approach

1. **Pragmatic.** Optimize for reaching a playable game. Recover formats and
   logic accurately enough to be correct; do not add reproducibility ceremony
   that does not move the port forward.
2. **Binary-only sources.** Recover behavior from `BUMPY.EXE` and the original
   files — not from community docs. Screenshots/video are for visual comparison
   only.

Game logic stays independent of SDL3, refresh rate, and floating point. SDL3 is
only a platform adapter.

## Architecture

- `core` — fixed tick, integer math, RNG, indexed framebuffer.
- `game` — menu, level, physics, collision, objects, state transitions.
- `resources` — direct readers/decoders for the original formats.
- `video` — palette and frame composition over an indexed 320×200 buffer.
- `audio` — music, instrument bank, effects.
- `platform_sdl3` — window, input, timing, presentation.

## Roadmap

- **Stage 1 — `.VEC` container + title screen — DONE.** `.VEC` decoder and the
  title bitmap render natively.
- **Stage 2 — Menu — DONE (renders + interactive).** The full 320×200 menu
  screen deplanes with its embedded VGA palette, and the selection cursor (the
  `FLECHE.BIN` arrow sprite) draws at the active row and tracks `cursor_row`.
  Resource bundle, menu state machine, and SDL3 shell build and run.
- **Stage 3 — First level — IN PROGRESS.** Level data formats recovered and
  visually verified, a **static composed board renders natively**, the **BUM entity
  sprites** draw from the uncompressed bank, and the **world-map screen is now
  wired in**: confirming "start" on the menu shows world 1's map
  (`MONDE1.VEC` + the Bumpy avatar on node 1); the arrows move between linked nodes;
  fire enters that node's board; Escape returns to the menu (see "Stage 3 world-map
  screen" below). The flow now matches the original's **menu → world map →
  playfield**; the temporary ←/→ board paging is retired. Remaining: physics,
  collision, win/loss; the in-level gameplay palette; the map's score/lives HUD. The
  sprite-frame decoder from Stage 2 is the reusable foundation for gameplay sprites.

## Current state

**Foundation (done, reusable):** SHA-256 asset manifest (50 files), reproducible
LZEXE unpack → `analysis/generated/BUMPY.UNPACKED.EXE`, Ghidra import (509
functions), DOSBox-X reference harness.

**Menu/resource pipeline (recovered, builds and runs on master):**
- `.VEC` decoder (`src/resources/vec`): 12-byte big-endian layer header with XOR
  checksum, multi-layer, **method 4** (marker-RLE) and **method 12**
  (marker-mask). All 13 supplied `.VEC` decode to the expected bytes (verified by
  SHA in `tests/cpp/vec_test.cpp`).
- `TITRE.VEC` → full 320×200 screen: 99-byte header (16-colour VGA palette at
  offset `0x33`) + four 8000-byte plane-sequential bit-planes, deplaned and
  rendered through `IndexedFramebuffer` (`src/video/menu_renderer`:
  `deplane_screen` / `apply_screen_palette`). Format spec:
  `analysis/specs/menu-resource-formats.md`.
- **Sprite-frame decoder (`src/resources/sprite_frame`)** — recovered by
  disassembling the overlay blitter (`1cec:31b7` → `1cec:2ded`/`2d6d`). Format:
  a BE32 pointer table over a `0x800`-based data region; each frame = 12-byte
  header (six BE16 words: mask, flags, x/y origin, width-units, height) + 4-plane
  row-interleaved VGA pixels (colour 0 = transparent). Width = `width_units*4`.
  Compressed frames (flags `0x40`/`0x20`) are NOT yet handled. Spec:
  `analysis/specs/menu-resource-formats.md`. **This decoder + format is the
  reusable foundation for all sprites** (BUMSPJEU bumpers, level sprites).
- **Menu cursor**: the arrow is `FLECHE.BIN` frame 0 (16×16, uncompressed),
  loaded in `MenuResources` and drawn by `MenuRenderer` at x 48, y 112+row·16,
  tracking `cursor_row`. (BUMSPJEU.BIN itself is the gameplay bumper sprites —
  see `analysis/generated/sprite_sheet.png`; same frame format, reuse in Stage 3.)
- Menu state machine (`src/game/menu`) and SDL3 shell (`src/platform_sdl3`).
- `bumpy_port.exe` renders the authentic title + cursor and runs the menu window;
  31 C++ test cases pass. `--render-title [out.bmp]` dumps the menu;
  `--dump-title-raw out.bin` dumps the raw decoded title bytes.

**Stage 3 progress (level formats recovered, visually verified):**
- All level blobs are **layered-VEC containers** decoded by the existing
  `src/resources/vec` decoder. Per-level init+draw is `FUN_1000_2d14`, which
  patches the level digit into `?.PAV`/`?.DEC`/`?.BUM` (level table `203b:0090`,
  indices 0/1/8) and decodes each via `FUN_1000_7b5a` (= the VEC decoder).
  Decoded sizes are fixed per type; full spec: `analysis/specs/level-formats.md`.
- **D?.PAV** = 6-byte header + **320×192 plane-sequential 4-plane VGA image** =
  the per-world object/sprite sheet (bear, balloon, candies, presents…),
  colour 0 transparent. Confirmed by eye. Drawn by `FUN_1000_0a90` from
  `PAV_buffer + 6`.
- **MONDE?.VEC** (9 files) = 32099-byte **screen-format** backgrounds (same as
  `TITRE.VEC`): per-world art + a 4×5 grid of node rings. Render successfully.
- **D?.DEC** = static tile grid: 2-byte header + **N×812-byte boards**
  (N=15 or 12). Each board = 32-byte header + **20 cols × 13 rows × 3 bytes/cell**
  (`6bca = DEC+2+board*0x32c`, read by `FUN_1000_2a0a`). Cell[0] = PAV object
  index (0=empty, 1..0xf0=single, ≥0xf1=stacked using cell[1..2]).
- **D?.BUM** = dynamic per-board entities ("bumpers"): 2-byte header +
  **N×194-byte boards** (matching N), each = 3×48-byte entity tables + 6 params
  (`75d0 = BUM+2+board*0xc2`, copied by `FUN_1000_32b0`). `D6.BUM`/`D9.BUM` ship
  **raw/pre-decoded**. Board counts verified: (12182−2)/812 = (2912−2)/194 = 15.
- New dev/inspection flags on `bumpy_port.exe`: `--decode-vec`, `--render-screen`,
  `--render-pav` (see spec).

**Stage 3 board module + renderer (builds and runs on master):**
- **`src/resources/level_resources`** — `LevelResources::load(root, n)` decodes
  `D{n}.PAV/DEC/BUM` via the VEC decoder with the raw-BUM fallback (D6/D9), exposes
  the board count, per-board 20×13×3 cells (column-major `0x20 + col*0x27 + row*3`),
  and the deplaned 320×192 PAV object sheet. Validated on levels 1/3/4/6/7/9
  (`tests/cpp/level_resources_test.cpp`), including the **D7 DEC/BUM count mismatch**
  (12 tile-boards vs 15 entity-boards) and level 3's empty PAV.
- **`src/video/board_renderer`** — `render_board` composes a static board: the
  base-tile pass as a flat colour-index-0 clear (the real overlay behavior, see
  below), then the DEC-placed PAV objects — single (`1..0xf0`) and stacked
  (`≥0xf1`) — on the per-world MONDE palette. `--render-board <level> <MONDE.VEC>
  <board> <out.bmp> [map]` dumps it (`analysis/generated/board_L1_B0.bmp`, verified
  by eye against the original world-1 art).
- **Blit geometry confirmed** (overlay `1ab9` planar path, recovered statically by
  Codex): width-unit = 16 px, height-unit = 8 px → playfield tiles are 16×16. The
  per-cell "base tile" (`FUN_1000_0b88`) reads no bitmap; it fills from command
  bytes `+0x22..+0x25`, which are zeroed → a flat colour-0 clear, **not** a floor
  sprite. The visible floor cross-hatch is the dense `0x63` PAV objects. Full
  recovery in `analysis/specs/level-formats.md` ("Base-Tile Blit Recovery").
- 38 C++ tests pass; originals verify clean.

**Stage 3 in-window wiring (builds and runs on master):**
- **`src/game/app`** — an SDL-independent top-level state machine (`App`) tying
  the menu to the in-level board view. Transitions: menu `--confirm "start"-->`
  level (board 0); menu `--cancel-->` quit; level `--cancel-->` menu; level
  `--left/right-->` page boards (wrap within `[0, board_count)`). The Menu keeps
  its own up/down/confirm debounce; `App` adds edge detection for the keys it owns
  (cancel, left/right) so a held key cannot bounce across a transition. 8 unit
  tests in `tests/cpp/app_test.cpp`.
- **`src/platform_sdl3/sdl_app`** now drives the `App`: it renders the menu via
  `MenuRenderer` on the menu screen and the static board via `render_board(level,
  board_index, MONDE1 backdrop)` on the level screen, and the event loop no longer
  special-cases Escape (the `App` owns cancel). `src/app/main.cpp` loads level 1 +
  `MONDE1.VEC` up front and constructs the `App` with the level's board count.
- Launching → menu; confirm "start" → level 1's board fills the window (the
  by-eye-verified world-1 art, `analysis/generated/board_L1_B0.png`); Escape →
  menu. The level screen is **display only** — no entities/physics yet.
- 46 C++ tests pass; originals verify clean.

**Stage 3 BUM entities (recovered + decoded):**
- **`D?.BUM` is three 8×6 entity layers + 6 params**, recovered from the spawn
  routine `FUN_1000_2a78` (iterates `cell = row*8 + col`, three independent
  per-layer draw paths) and the activation copy `FUN_1000_32b0`. Layer A =
  pegs/bumpers, layer B = second layer (col 7 unused), layer C = collectibles
  (sprite = `value + 0x179`). The cell→pixel coordinate table was extracted from
  the data segment (`DS:0x274`): columns 0–6 at `x = 8 + col*40`, rows at
  `y = 8 + row*32`. Full spec: `analysis/specs/level-formats.md` ("D?.BUM").
- **`src/resources/level_resources`** decodes a board into `BumEntities` (three
  8×6 layers + params + `bum_cell_position`), validated against the real `D1`
  board-0 values (peg pattern, collectible codes, params 41/44/6/0/9/0) in
  `tests/cpp/level_resources_test.cpp`.
- **`overlay_bum_entities`** (`src/video/board_renderer`) draws a marker per
  occupied cell at its faithful position for by-eye validation:
  `--render-board 1 MONDE1.VEC 0 out.bmp entities`
  (`analysis/generated/board_L1_B0_entities.png`, 27 pegs + 6 collectibles,
  landing correctly on the world-1 art). These are inspection markers, **not** the
  original entity sprites.
- 50 C++ tests pass; originals verify clean.

**Stage 3 real entity sprites (recovered + implemented):**
- The BUM entity sprites are drawn from the **uncompressed** `BUMSPJEU.BIN` bank.
  The selection chains were recovered from `FUN_1000_2a78`/`FUN_1000_165e` and the
  static descriptor tables in `BUMPY.UNPACKED.EXE`: layer A `value → 0x3d3a →
  DS:0x37be {count, frame}` (peg → frame `0x40`), layer C `frame = value + 0x179`.
  The master frame table is addressed directly by frame index — the existing
  `decode_sprite_frame(bumspjeu, idx)` already does `be32(idx*4)+0x800`.
- **`src/resources/entity_sprites`** holds the recovered tables + resolution;
  **`draw_bum_entities`** (`src/video/board_renderer`) blits the real frames at
  their faithful positions (`--render-board <level> <MONDE.VEC> <board> out.bmp
  sprites`, and live in the SDL window). Validated on `D1` board 0 (27 pegs + 6
  collectibles, 0 skipped) by test and by eye
  (`analysis/generated/board_L1_B0_sprites.png`). 53 C++ tests pass; originals
  verify clean. Full spec: `analysis/specs/level-formats.md` ("Entity sprite bank").
- **Sprite decoder plane-layout fix (`src/resources/sprite_frame`):** the decoder
  laid out a sprite row plane-major over the full width; the real format stores
  **16px groups** (`[g0:P0 P1 P2 P3][g1:…]`). The two coincide for 16px frames, so
  faces/collectibles were always correct, but every **32px** frame (bumper pegs, the
  node marker, the Bumpy-on-cloud avatar) decoded scrambled. Fixed to the group
  layout (the `1cec:0c77` reshuffle); the board's bumper sprites and the map avatar
  now render correctly. Spec: `analysis/specs/menu-resource-formats.md`.

**Stage 3 world-map screen (builds and runs on master):**
- The original's missing **menu → world map → playfield** flow is now in the port
  for world 1. **`src/game/world_map`** is a pure, SDL-free state machine over the
  baked world-1 node graph + positions (extracted from `BUMPY.UNPACKED.EXE`: graph
  `DS:0x09e6`, positions `DS:0x0a80`, `base + N*9` records; all 15 nodes verified
  against `analysis/specs/screen-flow.md`). Pressing a direction **glides** the
  avatar 4px/tick along the connecting line to the linked neighbour (input ignored
  mid-slide), fire selects board `node−1`, Escape returns to the menu. Recovered from
  `FUN_1000_3852` / `3a88` / `3ab2…3bc9` (the `dist>>2`-steps-of-4px animation) /
  `1cb2`.
- **`src/video/map_renderer`** composes the map: the `MONDE1.VEC` backdrop (via the
  factored **`src/video/screen_image`** deplane helper, shared with `board_renderer`)
  plus the **Bumpy-on-cloud avatar** — `BUMSPJEU.BIN` **frame `0x21`**
  (`FUN_1000_1cb2`/`DAT_824a`) — centred on the current node's ring. This frame
  matches `screenshots/bumpy_001.png` pixel-for-pixel (orange face + blue cloud). It
  only looked like garbage at first because the **sprite decoder used the wrong plane
  layout for 32px frames** — see the decoder fix below.
- **`src/game/app`** gains `Screen::map` between menu and level; the temporary ←/→
  board paging is **retired** (node selection replaces it). Held-key guards stop a
  held fire/cancel bouncing across a transition. **`--render-map <world> <MONDE.VEC>
  <out.bmp>`** dumps the map headlessly. 67 C++ tests pass; originals verify clean.
  Design + plan: `docs/superpowers/specs/2026-06-21-world-map-screen-design.md`,
  `docs/superpowers/plans/2026-06-21-world-map-screen.md`.

**Stage 3 world-map cloud-jump animation (builds and runs on master):**
- Fire on a node now plays the original's **fire-to-enter cloud-jump** before the board
  loads, recovered from `FUN_1000_3cf7` + the 22-record script at **DS:0x1114** (each
  record `{frame, dx, dy}`, applied one per tick by `FUN_1000_13df`). Bumpy squashes on
  his cloud, bounces up ~8px, arcs down ~8px, then vanishes (frame `100`). dx is 0 for
  every record → purely vertical. Full recovery: `analysis/specs/screen-flow.md`
  ("Selecting a node").
- **`src/game/world_map`** owns the jump state machine (`is_jumping()`, the baked
  24-step table incl. the two pre-loop frames); confirm starts it, `select_board`
  returns only when it finishes, `enter()` resets it. Input is ignored mid-jump (like a
  slide). New tests lock the frame/offset sequence and the input-ignored invariant.
- **`src/video/map_renderer`** draws `view.avatar_frame` + the launch cloud
  (`view.cloud_visible`). Placement was the subtle part: the resting avatar (frame
  `0x21`, 32×21) is a **ball + cloud composite**, and the jump reuses its parts
  standalone — frame `0` is pixel-identical to `0x21`'s top (content offset `(8,0)`),
  frame `0xcb` to its bottom (`(0,10)`). Frames are **centred horizontally on the node**
  in the `0x21` box (ball top-aligned + bounced, cloud bottom-aligned) so the jump
  starts exactly where the resting pose sits and the cloud stays put. (The frame-header
  `origin` words are **not** the composite anchor — origin-aligning lurches the parts
  ~(8,8), which the original does not.)
- **`--render-jump <node> <MONDE.VEC> <out-prefix>`** dumps the animation as
  `<prefix>NN.bmp` for by-eye verification. 69 C++ tests pass; originals verify clean.

**Placeholders / remaining work:**
- Compressed sprite frames (flags `0x40`/`0x20`, `1cec:2ded`) are **fully recovered
  from disassembly but unused by the supplied assets** — `BUMSPJEU` is all
  uncompressed and the compressed `BUMPYSPR.BIN`/`SPRITE.BIN` are not shipped. The
  decoder is documented (`analysis/specs/menu-resource-formats.md`) but not
  transcribed into the port (nothing to decode/validate).
- The menu sub-screens (HIGH-SCORE / PASSWORD draw per-glyph character sprites
  via the same archive) are not implemented.
- The in-window level screen is **static** — it shows the composed board with its
  BUM entity sprites but has no physics, collision, or win/loss yet (Stage 3
  remainder). The world map navigates (with the gliding Bumpy-on-cloud avatar),
  **plays the fire-to-enter cloud-jump animation** (see below), and selects boards, but
  its score/lives HUD overlays and the per-completed-node markers (frame `0x1da`) are
  not drawn yet.

## How to run

```powershell
cmake --build --preset windows-debug
& build/windows-debug/Debug/bumpy_port.exe                       # menu window
& build/windows-debug/Debug/bumpy_port.exe --render-title out.bmp  # headless dump
& build/windows-debug/Debug/bumpy_port.exe --decode-vec D1.PAV out.bin          # decode any VEC/PAV/DEC/BUM
& build/windows-debug/Debug/bumpy_port.exe --render-screen MONDE1.VEC out.bmp   # 320x200 screen-format VEC
& build/windows-debug/Debug/bumpy_port.exe --render-pav D1.PAV MONDE1.VEC out.bmp planeseq 320 192 6
& build/windows-debug/Debug/bumpy_port.exe --render-board 1 MONDE1.VEC 0 board.bmp        # static board (add 'map' to overlay the world-select screen)
& build/windows-debug/Debug/bumpy_port.exe --render-map 1 MONDE1.VEC map.bmp              # world-map screen (MONDE1 + Bumpy avatar on node 1)
```

In the window: confirm "start" on the menu → world map; arrows move Bumpy between
linked nodes; Enter/Space enters that node's board; Escape steps back (level → map's
menu, map → menu, menu → quit).

## Reverse-engineering workflow

1. Regenerate the decompilation if needed (`tools/re/decompile_loader.ps1` →
   `analysis/generated/decomp/all_functions.c`).
2. Read the relevant functions; correlate with strings, the resource table, and
   the raw bytes of the original files.
3. Record recovered behavior in `analysis/` (catalog + specs) with addresses.
4. Transcribe confirmed behavior into C++ behind a platform-independent boundary.
5. Test decoders/logic on the real files; compare visuals to the original by eye.

## Key recovered facts

- Built with **Turbo C++ 1990 (Borland)**, 16-bit real mode, large model. DOS
  calls appear as `swi(0x21)` in the decompilation.
- **Frame timing is vertical-retrace paced** (one logic tick per displayed frame,
  `7bdd` → `FUN_1ab9_0351` → the `0x3DA`-bit-3 retrace poll at file `0x11405`). For
  VGA 320×200 that is **70.086 Hz**. The PIT's ~19.2 kHz reprogramming is the
  PC-speaker sample timer, **not** the frame clock. The port paces the loop at
  70.086 Hz (`src/platform_sdl3/sdl_app`); see `screen-flow.md` ("Frame timing").
- Data segment is the load-relative `0x103b` (Ghidra shows `0x203b`, `+0x1000`).
- Resource/loader pipeline: `analysis/RESOURCE_PIPELINE.md`.
- `.VEC` and sprite formats: `analysis/specs/menu-resource-formats.md`.
- Level formats (PAV/DEC/BUM, MONDE): `analysis/specs/level-formats.md`.
- Screen flow + world map + palette: `analysis/specs/screen-flow.md`.

## Safety rules

- Never modify the original game files; treat root-level game files as read-only
  inputs (`python tools/assets/manifest.py verify`).
- Generated artifacts and downloaded tools live under ignored
  `analysis/generated/` and `tools/vendor/`. Do not commit them.

## Next step

**The world-map screen is DONE** (see "Stage 3 world-map screen" above): the port
now follows the original's **menu → world map → playfield** flow for world 1, with
node navigation and board selection; the `←/→` paging stand-in is retired. The
static composed board, the BUM entity layout, and the real entity sprites are all
DONE as well.

The next milestone is **making the board come alive** — the live gameplay loop:

1. **Physics + collision + win/loss** — Bumpy movement, bumping objects, the
   board-clear condition, and advancing through a level's 15/12 boards. Recover the
   in-level loop body in `FUN_1000_0c18` (after `FUN_1000_3852` returns the selected
   node) and the per-tick update/draw/input routines it calls.
2. **Run the board loop in-window** — replace the static `Screen::level` display
   (currently `render_board` + `draw_bum_entities`) with the live board loop, and
   return to the world map (then menu) on win/loss instead of only on Escape. The
   `src/game/app` shell and the `WorldMap → board node-1` selection are the hooks.
3. **In-level gameplay palette** — the board still renders under the MONDE *map*
   palette (so world 1 looks brown). Trace the real in-level palette load (the
   `0x23` EGA-register patch vs the DAC palette; see `screen-flow.md` "Palette
   mechanism"). The brown is **not a bug** — it is the faithful per-world MONDE
   palette — but the gameplay screen likely re-patches it.

World-map follow-ups (deferred this slice, low priority): the score/lives HUD on the
map (`FUN_1000_0816` digit formatter), completed-node markers (frame `0x1da`,
`FUN_1000_3c4f`), the avatar's idle cloud-bounce (the fire-to-enter cloud-jump is now
done — see "Stage 3 world-map cloud-jump animation"), and **worlds 2–9** (extract the
per-world graphs/positions at `0x10c8[world]` / `0x10ec[world]`, load MONDE/level on
demand — unlocked once win/loss advances worlds).

Optional menu polish: implement compressed sprites + per-glyph text to bring up
the HIGH-SCORE / PASSWORD sub-screens (`FUN_1000_0d9d` / `0f7a` / `11eb`).

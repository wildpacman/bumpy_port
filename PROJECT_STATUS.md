# Bumpy Port — Project Status

Source of truth for new sessions. Last updated: 2026-06-21.

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
  visually verified, and a **static composed board renders natively** (MONDE
  per-world palette + DEC-placed PAV objects over the flat base clear; see "Stage
  3 progress" below). Remaining: physics, collision, entities (BUM); win/loss;
  return to menu. The sprite-frame decoder from Stage 2 is the reusable foundation
  for gameplay sprites.

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

**Placeholders / remaining work:**
- Compressed sprite frames (flags `0x40`/`0x20`) are rejected, not decoded — the
  menu cursor does not need them, but other sprites may.
- The menu sub-screens (HIGH-SCORE / PASSWORD draw per-glyph character sprites
  via the same archive) are not implemented.
- `start_first_level` is a stub; no level loading yet (Stage 3).

## How to run

```powershell
cmake --build --preset windows-debug
& build/windows-debug/Debug/bumpy_port.exe                       # menu window
& build/windows-debug/Debug/bumpy_port.exe --render-title out.bmp  # headless dump
& build/windows-debug/Debug/bumpy_port.exe --decode-vec D1.PAV out.bin          # decode any VEC/PAV/DEC/BUM
& build/windows-debug/Debug/bumpy_port.exe --render-screen MONDE1.VEC out.bmp   # 320x200 screen-format VEC
& build/windows-debug/Debug/bumpy_port.exe --render-pav D1.PAV MONDE1.VEC out.bmp planeseq 320 192 6
& build/windows-debug/Debug/bumpy_port.exe --render-board 1 MONDE1.VEC 0 board.bmp        # static board (add 'map' to overlay the world-select screen)
```

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
- Data segment is the load-relative `0x103b` (Ghidra shows `0x203b`, `+0x1000`).
- Resource/loader pipeline: `analysis/RESOURCE_PIPELINE.md`.
- `.VEC` and sprite formats: `analysis/specs/menu-resource-formats.md`.
- Level formats (PAV/DEC/BUM, MONDE): `analysis/specs/level-formats.md`.

## Safety rules

- Never modify the original game files; treat root-level game files as read-only
  inputs (`python tools/assets/manifest.py verify`).
- Generated artifacts and downloaded tools live under ignored
  `analysis/generated/` and `tools/vendor/`. Do not commit them.

## Next step

The **static composed board is DONE** (above): `level_resources` + `board_renderer`
compose a board from the recovered formats and it matches the original world-1 art
by eye. Grid/placement, PAV tile geometry, the base-tile clear, and the gameplay
palette are all resolved (`analysis/specs/level-formats.md`).

The next milestone is **making the board come alive**:

1. **BUM entities** — pin down the three 48-byte per-entity tables + 6 params
   (`FUN_1000_32b0` working buffer `203b:a0e4`), then spawn the dynamic
   objects/bumpers onto the rendered board.
2. **Physics + collision + win/loss** — Bumpy movement, bumping objects, the
   board-clear condition, and advancing through a level's 15/12 boards.
3. **Wire it up** — replace the `start_first_level` stub: load level 1, run the
   board loop, and return to the menu on win/loss.

Notes for that work:
- The **compressed sprite-frame path** (flags `0x40`/`0x20`, expanded in
  `1cec:2ded`) is still unimplemented and will be needed for animated gameplay
  sprites (`BUMSPJEU.BIN`, `BUMPYSPR.BIN`).
- Per-world palette plumbing: the MONDE palette matches by eye, but the exact
  resource the live game installs as the gameplay palette is not yet traced.

Optional menu polish: implement compressed sprites + per-glyph text to bring up
the HIGH-SCORE / PASSWORD sub-screens (`FUN_1000_0d9d` / `0f7a` / `11eb`).

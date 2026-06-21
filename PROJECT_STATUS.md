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
  visually verified (see "Stage 3 progress" below). Remaining: grid/object
  placement semantics, physics, collision, objects; win/loss; return to menu.
  The sprite-frame decoder from Stage 2 is the reusable foundation for gameplay
  sprites.

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
- **D?.BUM** = `0x100e` header + 1-byte object/tile-index grid (cells 0–0x2e).
  `D6.BUM`/`D9.BUM` ship **raw/pre-decoded** (no VEC wrapper). Grid dims TBD.
- **D?.DEC** = `0x000e` header + palette-ish bytes + 3-byte placement records.
  Record layout TBD.
- New dev/inspection flags on `bumpy_port.exe`: `--decode-vec`, `--render-screen`,
  `--render-pav` (see spec). 31 C++ tests still pass; originals verify clean.

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

Stage 3 level **formats** are recovered and visually verified (above). The next
concrete milestone is a **static composed level**: MONDE background + PAV objects
placed by the BUM grid, rendered in the port and compared to the original by eye.
To do that, the remaining recovery is:

1. **Grid + placement semantics** — confirm the BUM grid dimensions and the DEC
   3-byte record layout from the gameplay consumers (`FUN_1000_3467`, the
   per-frame draw chain in `FUN_1000_0c18`, buffers `203b:75de`/`6bca`), and the
   level palette source (MONDE vs DEC). See `analysis/specs/level-formats.md`
   open questions.
2. **Compose + render** — add a `level` module (decode PAV/DEC/BUM with the
   raw-BUM fallback for D6/D9) + a level renderer; dump a static level frame and
   compare by eye.
3. **Then** physics, collision, objects, win/loss, and return to menu; wire
   `start_first_level`.

Gameplay sprite note: the **compressed frame path** (sprite-frame flags
`0x40`/`0x20`, expanded in `1cec:2ded`) is still unimplemented and will be needed
for animated gameplay sprites (`BUMSPJEU.BIN`, `BUMPYSPR.BIN`).

Optional menu polish: implement compressed sprites + per-glyph text to bring up
the HIGH-SCORE / PASSWORD sub-screens (`FUN_1000_0d9d` / `0f7a` / `11eb`).

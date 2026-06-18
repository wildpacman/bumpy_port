# Bumpy Port — Project Status

Source of truth for new sessions. Last updated: 2026-06-19.

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
- **Stage 2 — Menu — mostly done.** Resource bundle, menu state machine, cursor
  logic, and SDL3 shell exist and build. Remaining: the real VGA DAC palette and
  sprite pixel rendering.
- **Stage 3 — First level — next.** `.BUM`/`.DEC`/`.PAV` formats, physics,
  collision, objects; win/loss; return to menu.

## Current state

**Foundation (done, reusable):** SHA-256 asset manifest (50 files), reproducible
LZEXE unpack → `analysis/generated/BUMPY.UNPACKED.EXE`, Ghidra import (509
functions), DOSBox-X reference harness.

**Menu/resource pipeline (recovered, builds and runs on master):**
- `.VEC` decoder (`src/resources/vec`): 12-byte big-endian layer header with XOR
  checksum, multi-layer, **method 4** (marker-RLE) and **method 12**
  (marker-mask). All 13 supplied `.VEC` decode to the expected bytes (verified by
  SHA in `tests/cpp/vec_test.cpp`).
- `TITRE.VEC` → 320×200 four-plane planar bitmap, rendered through
  `IndexedFramebuffer` (`src/video/menu_renderer`).
- `BUMSPJEU.BIN` sprite archive layout (`src/resources/menu_resources`): 3 groups
  × 33 = 99 offset-delimited children.
- Menu state machine (`src/game/menu`) and SDL3 shell (`src/platform_sdl3`).
- `bumpy_port.exe` renders the title and runs the menu window; 28 C++ test cases
  pass. `bumpy_port.exe --render-title out.bmp` dumps the title headlessly.

**Placeholders / remaining work:**
- **Palette is a preview** (`apply_preview_palette` in `main.cpp`). The real VGA
  DAC palette — the 16-byte table copied into the decoded title at offset `0x23`
  — is not wired in yet, so colors are not authentic.
- Sprites are decoded structurally but not yet rendered to pixels.
- `start_first_level` is a stub; no level loading yet.

## How to run

```powershell
cmake --build --preset windows-debug
& build/windows-debug/Debug/bumpy_port.exe                       # menu window
& build/windows-debug/Debug/bumpy_port.exe --render-title out.bmp  # headless dump
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

## Safety rules

- Never modify the original game files; treat root-level game files as read-only
  inputs (`python tools/assets/manifest.py verify`).
- Generated artifacts and downloaded tools live under ignored
  `analysis/generated/` and `tools/vendor/`. Do not commit them.

## Next step

Recover the real VGA DAC palette (the 16-byte table at decoded title offset
`0x23` and its DAC mapping; trace `1000:7b93` / `1000:08d1`) and wire it into the
renderer for authentic colors. Then render the menu sprites, then begin Stage 3
(level 1). See `docs/superpowers/plans/stage-1-vec-and-title-screen.md` for the
recovered context.

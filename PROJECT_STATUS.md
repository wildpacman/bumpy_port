# Bumpy Port — Project Status

Source of truth for new sessions. Last updated: 2026-06-19.

## Goal

A **playable** native Windows 11 port of *Bumpy's Arcade Fantasy* in C++/SDL3
that reads the original game's resources directly. The port should feel faithful
to the original — same look, timing, and behavior — without being a
bit-for-bit preservation project.

First slice (definition of done): the original menu and the first level are
playable from launch through win/loss and back to the menu, reading the supplied
original files.

## Approach

Two principles drive every decision:

1. **Pragmatic.** Optimize for reaching a playable game. Recover formats and
   logic accurately enough to be correct; do not add reproducibility ceremony
   (dual-tool agreement, pixel-exact gates, per-syscall dynamic proofs) that does
   not move us toward a playable build.
2. **Binary-only sources.** Recover behavior from `BUMPY.EXE` and the original
   resource files — not from community docs or third-party reverse engineering.
   Screenshots and video are used only for visual comparison.

Game logic stays independent of SDL3, the monitor refresh rate, and floating
point. SDL3 is only a platform adapter (window, input, timer, audio, present).

## Architecture

- `core` — fixed game tick, integer math, RNG, indexed framebuffer.
- `game` — menu, level, physics, collision, objects, state transitions.
- `resources` — direct readers/decoders for the original file formats.
- `video` — palette and frame composition over an indexed 320×200 buffer.
- `audio` — music, instrument bank, effects.
- `platform_sdl3` — window, input, timing, presentation.

## Roadmap

- **Stage 1 — `.VEC` container + title screen.** Recover the resource load +
  draw path and the `.VEC` pixel/palette format from the binary; implement a C++
  decoder; render `TITRE.VEC`. One decoder unlocks title, masks, worlds, score,
  and level graphics.
- **Stage 2 — Menu.** Composition, palette, cursor (`FLECHE.BIN`), font
  (`DDFNT2.CAR`), input, transitions — from the menu code.
- **Stage 3 — First level.** `.BUM`/`.DEC`/`.PAV` formats, physics, collision,
  objects; win/loss; return to menu.

Each stage is a short, lean plan. Verify a format by decoding every file of that
format to full consumption, and verify visuals by eye against the original.

## Current state

**Foundation (done, reusable):**
- SHA-256 manifest of all 50 original files (`config/original-assets.sha256`).
- Reproducible LZEXE 0.91 unpack → `analysis/generated/BUMPY.UNPACKED.EXE`
  (112336 bytes).
- Ghidra 12.1.2 import of the unpacked image (509 functions).
- DOSBox-X reference harness (`tools/reference/run_reference.ps1`).
- C++20/CMake/SDL3 runtime shell with `IndexedFramebuffer`, asset-manifest
  reader, and an SDL window.

**Reverse-engineering tooling:**
- `tools/re/decompile_loader.ps1` — one-pass PyGhidra export (reuses the
  downloaded Ghidra + JDK) → `analysis/generated/decomp/all_functions.c`
  (all 509 functions as readable C), `strings.txt`, `index.txt`.

**Stage 1 (in progress):** the resource/loader pipeline is recovered from the
binary — see `analysis/RESOURCE_PIPELINE.md`. Confirmed: the 10-byte resource
table, the open→read→close path, and that `.VEC` files load **raw** (pixel decode
happens at draw time). Game logic ported so far: none.

## Reverse-engineering workflow

1. Regenerate the decompilation if needed (`tools/re/decompile_loader.ps1`).
2. Read the relevant functions in `analysis/generated/decomp/all_functions.c`;
   correlate with strings, the resource table, and original file bytes.
3. Record the recovered behavior in `analysis/` (catalog + notes) with addresses.
4. Transcribe confirmed behavior into C++ behind a platform-independent boundary.
5. Test decoders/logic on the real original files; compare visuals to the
   original by eye. Investigate any mismatch as a port defect.

## Key recovered facts

- `BUMPY.EXE` is a DOS MZ executable packed with LZEXE 0.91.
- Built with **Turbo C++ 1990 (Borland)**, 16-bit real mode, large/far-data
  model. DOS calls appear as `swi(0x21)` in the decompilation.
- Ghidra load base is segment `0x1000`. The data segment is the load-relative
  `0x103b`, which Ghidra shows as `0x203b` (`+0x1000`). File offset `F` maps to
  Ghidra linear `0x10000 + (F − 0x1090)`.
- Resource pipeline (table format, open/read/close, draw entry points) is
  documented in `analysis/RESOURCE_PIPELINE.md`.

## Repository layout

```
BUMPY.EXE, *.VEC, *.BUM, ...   original game files (read-only inputs)
config/original-assets.sha256   manifest of the original files
src/                            C++ port (core/game/resources/video/platform_sdl3)
tests/                          C++ (Catch2) and Python tests
tools/assets/                   asset manifest verifier
tools/re/                       reverse-engineering tools (decompile_loader.ps1, mz, unpack)
tools/reference/                DOSBox-X reference harness
analysis/catalog/               function/global catalogs (addresses + status)
analysis/RESOURCE_PIPELINE.md   recovered resource/loader map
analysis/generated/             IGNORED: unpacked exe, decompilation, downloaded tools
```

## Safety rules

- Never modify the original game files; treat every root-level game file as a
  read-only input. `python tools/assets/manifest.py verify` checks them.
- Generated artifacts and downloaded tools live under ignored
  `analysis/generated/` and `tools/vendor/`. Do not commit them.

## Verification

`tools/verify.ps1` checks asset integrity, the unpack, the Python tests, the
CMake build, and the C++ tests. Run it before and after work that touches the
build or the original files.

## Next step

Read `FUN_1000_7b5a` (blit) and `FUN_1000_7b93` (palette) in the decompilation to
recover the exact `.VEC` pixel encoding and palette, then implement
`src/resources/vec` and render `TITRE.VEC`. See
`docs/superpowers/plans/stage-1-vec-and-title-screen.md`.

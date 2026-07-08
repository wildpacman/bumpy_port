# Bumpy Port

A native Windows 11 port of *Bumpy's Arcade Fantasy* (1993, DOS) in C++20 / SDL3,
reading the original game's own resource files directly — no re-implemented art,
no upscaled/AI-generated replacements. Faithful to the original's look, timing,
and behavior; not a bit-for-bit preservation project.

For the full reverse-engineering history, recovered formats, and stage-by-stage
status, see **`PROJECT_STATUS.md`** (the source of truth for development). This
file is the quick-start: build, run, controls, config, dev tools.

## Requirements

- The original game's resource files at the repo root: `BUMPY.EXE`, `TITRE.VEC`,
  `MONDE1..9.VEC`, `D1..9.PAV/DEC/BUM`, `BUMSPJEU.BIN`, `DDFNT2.CAR`, `BUMPY.MID`,
  `BUMPY.BNK`, `SCORE.VEC`, `DESSFIN.VEC`, `FLECHE.BIN`, `MASKBUMP.VEC`, `BUMPRESE.VEC`. These are
  **not** included in this repository (see `.gitignore`) — supply your own copy of
  the original game and drop the files in. They are treated as read-only inputs;
  the port never writes to them.
- CMake >= 3.25, a C++20 compiler (MSVC / Visual Studio 2026 on Windows).
  Dependencies (SDL3, Catch2, ymfm) are fetched automatically via CMake
  `FetchContent` — nothing to install by hand.
- A GPU with OpenGL 3.3 core support for the 3D render mode (optional — see
  below; the port falls back automatically without it).

## Build & run

```powershell
cmake --preset windows-debug               # One-time configure (required on fresh clone)
cmake --build --preset windows-debug      # Debug config, console kept for dev CLI flags
cmake --build --preset windows-release    # Release config, windowed (no console)
& build/windows-debug/Debug/bumpy_port.exe
& build/windows-debug/Release/bumpy_port.exe
ctest --preset windows-debug              # run the test suite
```

Both presets configure from the same `windows-debug` CMake preset and only differ
in build configuration, so both exes land under `build/windows-debug/{Debug,Release}/`.
`shaders3d/` (the 3D render mode's GLSL sources) is copied next to the exe by the
build automatically.

## Controls

| Key | Action |
|---|---|
| Arrows | Move / navigate menus and the world map |
| Enter / Space | Confirm / fire |
| Escape | Context-sensitive: quits from the menu, drops a life and returns to the world map from a level, GAME OVER from the map (matches the original's two-step exit) |
| Alt+Enter | Toggle fullscreen |
| Alt+A | Toggle display aspect: 16:10 square pixels (default) / 4:3 CRT-style |
| Alt+3 | Toggle the **3D diorama render mode** (in-level only; see below) |
| Alt+R | *(Debug builds only)* Hot-reload the 3D diorama's shaders from `shaders3d/` without restarting — a broken shader edit keeps the previous working programs instead of crashing |

Alt+Enter, Alt+A, and Alt+3 all persist their setting to `bumpy_port.cfg` and are
restored on the next launch.

## 3D render mode

An optional OpenGL 3.3 "diorama" presentation of the in-level playfield: the same
original sprites and board art, arranged with real depth, a light parallax
camera that follows the ball, and a soft spotlight/vignette/shadow pass — no
upscaled or new art. It only dresses the in-level playfield; the menu, world map,
and other screens still render flat even with 3D on. Toggle it with **Alt+3**, or
start already in 3D with **`--render3d`** on the command line. See
`PROJECT_STATUS.md` ("3D render mode") for the full design and recovery notes.

## Configuration file

`bumpy_port.cfg` is a plain `key=value` text file the port writes next to its own
exe. It is the port's **only** on-disk persistence (high scores stay session-only,
matching the original — there is no save file). It currently holds:

```
render3d=0        # Alt+3
square_pixels=1   # Alt+A (1 = 16:10, 0 = 4:3)
fullscreen=0      # Alt+Enter
```

Safe to hand-edit or delete; unknown keys are ignored and bad values fall back to
defaults, so files from older or newer builds are always tolerated.

## Developer / inspection tools

`bumpy_port.exe` doubles as a CLI for headless resource decoding and rendering,
used throughout development to verify recovered formats by eye or by byte-exact
comparison. The most relevant for the 3D render mode:

- `--render3d` — launch the normal window already in 3D diorama mode.
- `--render-3d <level> <MONDE.VEC> <board> <out.bmp>` — dump one diorama frame to
  a BMP headlessly, e.g. `--render-3d 1 MONDE1.VEC 0 out.bmp`.
- `--present-parity` — the flat-presentation parity gate: renders representative
  menu/board/map frames through the GL flat path at x2/x4 integer scale and
  compares byte-for-byte against the CPU nearest-neighbor reference (all PASS,
  exit 0, is the expected result).

See `PROJECT_STATUS.md` ("How to run") for the complete list of `--render-*`,
`--decode-vec`, `--dump-music`/`--dump-sfx`, and other inspection flags covering
every recovered resource format.

## Safety rules

- Never modify the original game files; they are read-only inputs.
- Generated artifacts and vendored tools live under ignored `analysis/generated/`
  and `tools/vendor/` — do not commit them.

# Bumpy's Arcade Fantasy — Port Design

## Goal

Build a playable native port of the DOS game *Bumpy's Arcade Fantasy* for
Windows 11 in C++ and SDL3. The port reads the original game resources directly
and reproduces the original's look, timing, and behavior closely enough to feel
faithful. It is a playable port, not a bit-exact preservation reimplementation.

The first milestone is a vertical slice: the port shows the original menu, lets
the player select and fully complete the first level, handles win/loss, and
returns to the menu.

## Method

Recover behavior from the DOS binary itself, not from videos or third-party
descriptions. For each subsystem the first slice needs:

1. Disassemble/decompile the relevant machine code.
2. Recover the functions, data, structures, and call graph involved.
3. Document the recovered behavior with binary addresses.
4. Transcribe the algorithm into clear modern C++ without changing its meaning.
5. Check the result against the original — by running it and by eye.

Source rule: recovery is grounded in `BUMPY.EXE` and the original files.
Screenshots and video are used only to compare observable output. Do not infer
file formats, palettes, or logic from screenshots.

Pragmatism rule: aim for a playable, faithful build. Spend effort on recovering
real behavior, not on reproducibility ceremony (dual-tool agreement, pixel-exact
gates, per-syscall dynamic proofs) that does not move the port forward.

## Source materials

In the project root are the original DOS binary and resources:

- `BUMPY.EXE` — the DOS binary, packed with LZEXE 0.91.
- `BUMP-Y.EXE` — a self-extracting archive of the game files.
- `.VEC`, `.BUM`, `.DEC`, `.PAV`, `.BIN`, `.CAR` — game resources.
- `BUMPY.MID`, `BUMPY.BNK` — music and instrument bank.

Original files are read-only inputs. Derived analysis artifacts and decompilation
output are kept separately under ignored directories.

## Architecture

The runtime is split into independent modules:

- `core` — fixed game tick, game state, integer math, RNG, indexed framebuffer.
- `game` — menu, level, physics, collision, objects, state transitions.
- `resources` — direct readers/decoders for the original formats.
- `video` — palette and frame composition over an indexed 320×200 buffer.
- `audio` — music, instrument bank, effects.
- `platform_sdl3` — window, input events, timer, presentation, audio device.

Game logic does not depend on SDL3, the monitor refresh rate, or floating point.
SDL3 is only a platform adapter. Rendering never affects the simulation.

## Compatibility rules

- Preserve the original's integer widths, overflow, and evaluation order where
  they affect observable behavior.
- Run the simulation at the recovered fixed tick rate.
- Convert input to original-style control signals before the tick.
- Read resource formats directly, with the same interpretation rules.
- Reproduce noticeable original behavior (including quirks) first; isolate any
  intentional modernization later.

## Accuracy verification

Keep, per recovered subsystem:

- the address, purpose, and status of each recovered function;
- format specifications with worked examples from the supplied files;
- automated tests for decoders, physics, and collision on the real files;
- visual comparison of port output against the original at key states.

Distinguish confirmed behavior from working hypotheses. A format is "recovered"
only when its bytes, fields, commands, and termination are explained and it
decodes every supplied file of that format. When automatic checking is
impractical, document why and verify by eye.

## First-slice flow

1. Launch. 2. Show the menu. 3. Select the first world/level. 4. Load the needed
original resources. 5. Play the first level fully. 6. Win or lose. 7. Return to
the menu. Subsystems not reachable on this path are out of scope for the slice.

## Out of scope (first slice)

Remaining levels and worlds; widescreen/arbitrary resolutions; gamepad support;
enhanced graphics or sound; fixing original bugs; a level editor; redistributing
the original resources.

## Risks and mitigations

- **16-bit analysis is fiddly.** Work from the decompilation; anchor on the
  recovered segment map and Turbo C++ conventions.
- **Unknown resource formats.** Recover loaders/decoders from the binary and
  validate decoders on every supplied file of that format.
- **Hidden DOS-timing dependence.** Keep simulation separate from output;
  confirm the tick rate from the original where it matters.
- **Audio accuracy is hard.** Do not treat audio as a blocker for early
  analysis, but include it in the slice's completion criteria.
- **Legal.** Work only with user-supplied files; never redistribute them.

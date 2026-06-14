# Bumpy Accurate Port: Project Status

Last updated: 2026-06-15

## Start Here

This file is the operational handoff for new sessions.

- Project root with original game files: `C:\dev\BUMPY`
- Current checkout: `C:\dev\BUMPY`
- Current branch: `master`
- Design:
  `docs/superpowers/specs/2026-06-13-bumpy-accurate-port-design.md`
- Completed foundation plan:
  `docs/superpowers/plans/2026-06-13-reverse-engineering-foundation.md`

Create a new isolated worktree before implementing the next milestone. Use
`master` only to read status, create the next plan, and integrate completed
work.

## Goal

Create an accurate native Windows 11 port of Bumpy's Arcade Fantasy using
C++ and SDL3. The port must read the supplied original resources directly and
preserve the original game's integer behavior, timing, limitations, and bugs.

The first vertical slice ends when the original menu and first level are
playable from launch through win/loss and return to menu.

## Current Milestone

Milestone: **resource formats and accurate menu**.

Current task: execute Task 1 of
`docs/superpowers/plans/2026-06-15-resource-formats-and-menu.md` in a new
isolated worktree: export and confirm the original file I/O and menu call path.

The reverse-engineering foundation milestone is complete and merged into
`master`. The next milestone starts by recovering the original file-opening
and file-reading functions from the Ghidra catalog, then specifies the `.VEC`
and other menu resource formats, and ends with a pixel-comparable original
menu rendered by the native SDL3 runtime.

## Progress

| Task | Status | Commits | Result |
|---|---|---|---|
| 1. Protect original assets | Complete | `ec7d4f5`, `6599eae` | Deterministic SHA-256 manifest for all 50 supplied files |
| 2. Strict DOS MZ inspection | Complete | `654007f`, `56334b7`, `8872c29` | Safe MZ parser and atomic deterministic report |
| 3. Pin research tools | Complete | `4ce56d6`, `9a7d84d`, `fcb7a7c` | Hash-pinned archives and pristine vendor checkouts |
| 4. Reproducible EXE unpacking | Complete | `2f125ed`, `ea5f060` | Two independent unpackers agree on normalized execution semantics |
| 5. Ghidra database/catalog | Complete | `e916baa`, `74dc91e`, `ddca578`, `4104bad` | Reproducible clean imports and approved address-linked catalog |
| 6. DOSBox-X reference run | Complete | `995b725` | Fixed reference environment and automated menu probe |
| 7. C++20/CMake scaffold | Complete | `6f947d4`, `6cb3648` | SDL-independent indexed framebuffer and native test scaffold |
| 8. Asset validation and SDL3 window | Complete | `6f613da` | Validated runtime shell with SDL3 output |
| 9. Unified milestone verification | Complete | `39e4121`, `9350555` | One-command milestone verification and stable artifact line endings |

All foundation tasks are complete. `tools/verify.ps1` passes on `master`.

## Next Milestone

Working name: `resource-formats-and-menu`.

Required outcome:

1. Recover and document the original file-opening and file-reading functions
   with addresses and evidence in the analysis catalog.
2. Identify the exact original resources used by the VGA game menu.
3. Specify and test `.VEC` and every other format needed by that menu path.
4. Implement SDL-independent C++ resource decoders that read the supplied
   original files directly.
5. Recover the menu palette, composition, cursor behavior, and input handling.
6. Render the original menu through `IndexedFramebuffer`.
7. Compare captured original and native menu frames automatically.
8. Add one command that verifies the menu milestone.

This milestone must recover behavior from the binary and resources. Do not
approximate the menu from screenshots.

Product decision: the native port starts directly on the confirmed VGA
game-menu path. It does not reproduce the DOS startup EGA/VGA selector. The
reference harness selects VGA only as setup before capturing/comparing the
resource-backed menu.

## Verified Findings

- `BUMPY.EXE` is a DOS MZ executable packed with LZEXE 0.91.
- Packed `BUMPY.EXE` SHA-256:
  `fcf4f4837e649efc9b4a4d8d1f7c8fc8644a9894f7dbd530d228d5462270b7a0`.
- The supplied set contains 50 original files tracked by
  `config/original-assets.sha256`.
- Primary unpacker:
  `samrussell/unpacklzexe@3a1b8b54e63e7e03181916d40acf7626d5558f6d`.
- Independent validator:
  historical Kou Kurizono UNLZEXE C implementation at
  `mywave82/unlzexe@066aac7be3b27813c221d3b03621ad6dfaecd285`.
- The official LZEXE companion `UPACKEXE.EXE` is not an LZEXE decompressor. It
  handles Microsoft EXEPACK and must not be used as the independent validator.
- The unpackers agree on normalized execution semantics:
  load image, ordered relocations, CS:IP, SS:SP, MinBSS, and MaxBSS.
- Normalized load image:
  - size: `108096` bytes
  - SHA-256:
    `a581f932352cd3bdd31f800819fd32d6b727caa46d1771a4f375d842e464083b`
- The primary unpacker's MinBSS calculation was incorrect. The wrapper
  normalizes it from original LZEXE 0.91 stub metadata using the historical
  UNLZEXE formula. The confirmed value is `211` paragraphs.
- Ghidra 12.1.2 imports the validated unpacked executable with loader
  `MzLoader` (`Old-style DOS Executable (MZ)`) and language/compiler
  `x86:LE:16:Real Mode:default`.
- Initial Ghidra analysis identifies 509 functions. The stable exported catalog
  SHA-256 is
  `d9d8178b51833b5799ce8f37bda3f5faf5c60fdd11a79ac065d31361a6df760a`.
- Two independent clean Ghidra projects produce the same raw discovery
  SHA-256:
  `4620fc53924f5e4c63671e8f09fc60d035ce3624fc1ecea50278f7ea3e6f0c6e`.
- Each clean analysis emits the same known Ghidra decompiler warning exactly
  twice: `Decompiling 1000:35a5, pcode error at 1000:84d7: Unable to resolve
  constructor at 1000:84d7`. Ghidra reports successful analysis and import in
  the same logs. No broader effect has been established.
- Ghidra rejects project paths containing a component that begins with `.`.
  Because the active worktree is below `.worktrees`, headless commands use a
  temporary junction path without dotted components while project files remain
  physically under the ignored `analysis/generated/ghidra-clean-1` and
  `analysis/generated/ghidra-clean-2` directories.
- Every Task 5 verification run cleanly extracts hash-verified Ghidra and JDK
  archives into ignored `analysis/generated/ghidra-tools`, verifies pinned
  `py.exe` and Python 3.12.0 executable hashes, and creates a clean PyGhidra
  virtual environment.

See `analysis/reports/mz-header.json` and
`analysis/reports/unpack-validation.json` for machine-readable evidence.

## Safety Rules

- Never modify the original game files.
- Original files in the worktree are hard links to files in `C:\dev\BUMPY`.
  Treat every root-level game file as read-only.
- Run `python tools/assets/manifest.py verify` before and after operations that
  touch game files.
- Generated files and downloaded tools belong under ignored
  `analysis/generated/` and `tools/vendor/`.
- Vendor git checkouts must remain at their pinned commits and pristine.
- Do not commit original game files, generated binaries, Ghidra projects, or
  downloaded tools.
- Do not work directly on `master`.

## Current Verification

The full Python suite contains 50 passing tests. The native C++ suite contains
3 passing test cases.

Run:

```powershell
Set-Location C:\dev\BUMPY
& tools/verify.ps1
git status --short
```

Expected:

- asset verification, all Python tests, unpack validation, CMake build, and C++
  tests pass;
- the Ghidra function catalog is non-empty;
- tracked worktree is clean.

## How To Continue

1. Open a new session in `C:\dev\BUMPY`.
2. Read this file, the design, and the completed foundation plan.
3. Confirm the branch is `master` and run `tools/verify.ps1`.
4. Use the brainstorming and writing-plans skills to create
   `docs/superpowers/plans/2026-06-15-resource-formats-and-menu.md`.
5. Review the new plan against the design and the required outcome above.
6. Create an isolated worktree/branch for executing the new plan.
7. Execute Task 1 first; do not implement resource decoders before the
   file-I/O/menu-path evidence and resource list are confirmed.
8. Update this file after each completed task or important discovery.

Suggested new-session prompt:

```text
Continue the Bumpy accurate-port project from PROJECT_STATUS.md in
C:\dev\BUMPY. Verify the completed foundation checkpoint, then create the
detailed resource-formats-and-menu implementation plan. The plan must begin
with recovering the original file-opening and file-reading functions from the
Ghidra catalog and end with a pixel-comparable original menu in the SDL3
runtime. Do not approximate behavior from screenshots.
```

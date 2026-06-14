# Bumpy Accurate Port: Project Status

Last updated: 2026-06-14

## Start Here

This file is the operational handoff for new sessions.

- Project root with original game files: `C:\dev\BUMPY`
- Active implementation worktree:
  `C:\dev\BUMPY\.worktrees\reverse-engineering-foundation`
- Active branch: `reverse-engineering-foundation`
- Base branch: `master`
- Design:
  `docs/superpowers/specs/2026-06-13-bumpy-accurate-port-design.md`
- Implementation plan:
  `docs/superpowers/plans/2026-06-13-reverse-engineering-foundation.md`

Run all development commands from the active implementation worktree unless a
task explicitly says otherwise.

## Goal

Create an accurate native Windows 11 port of Bumpy's Arcade Fantasy using
C++ and SDL3. The port must read the supplied original resources directly and
preserve the original game's integer behavior, timing, limitations, and bugs.

The first vertical slice ends when the original menu and first level are
playable from launch through win/loss and return to menu.

## Current Milestone

Milestone: reverse-engineering foundation.

Current task: **Task 6 - Configure a reproducible DOSBox-X reference run.**

Task 5 created an ignored Ghidra project under `analysis/generated/ghidra` and
an address-linked catalog containing 509 initial functions. All entries remain
`unknown` until later reverse-engineering work supplies stronger evidence.

## Progress

| Task | Status | Commits | Result |
|---|---|---|---|
| 1. Protect original assets | Complete | `ec7d4f5`, `6599eae` | Deterministic SHA-256 manifest for all 50 supplied files |
| 2. Strict DOS MZ inspection | Complete | `654007f`, `56334b7`, `8872c29` | Safe MZ parser and atomic deterministic report |
| 3. Pin research tools | Complete | `4ce56d6`, `9a7d84d`, `fcb7a7c` | Hash-pinned archives and pristine vendor checkouts |
| 4. Reproducible EXE unpacking | Complete | `2f125ed`, `ea5f060` | Two independent unpackers agree on normalized execution semantics |
| 5. Ghidra database/catalog | Complete | - | 509 address-linked functions exported from verified MZ analysis |
| 6. DOSBox-X reference run | Pending | - | Next task |
| 7. C++20/CMake scaffold | Pending | - | - |
| 8. Asset validation and SDL3 window | Pending | - | - |
| 9. Unified milestone verification | Pending | - | - |

Tasks 1-4 passed separate spec-compliance and code-quality reviews. Task 5 was
executed directly and verified by a clean Ghidra import plus a stable rerun of
the export script.

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
- Ghidra rejects project paths containing a component that begins with `.`.
  Because the active worktree is below `.worktrees`, headless commands use a
  temporary junction path without dotted components while project files remain
  physically under the ignored `analysis/generated/ghidra` directory.

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

The full Python suite contains 42 passing tests.

Run:

```powershell
Set-Location C:\dev\BUMPY\.worktrees\reverse-engineering-foundation
python tools/assets/manifest.py verify
python -m unittest discover -s tests/python -v
python tools/re/validate_unpack.py
git -C tools/vendor/unpacklzexe status --porcelain
git -C tools/vendor/unlzexe status --porcelain
git diff --check
git status --short --branch
```

Expected:

- asset verification exits `0`;
- 42 Python tests pass;
- unpack validation exits `0`;
- both vendor status commands print nothing;
- `git diff --check` prints nothing;
- tracked worktree is clean.

## How To Continue

1. Open a new session in:
   `C:\dev\BUMPY\.worktrees\reverse-engineering-foundation`.
2. Read this file, the design, and the implementation plan.
3. Confirm the branch is `reverse-engineering-foundation`.
4. Run the current verification commands above.
5. Continue Task 6 directly.
6. After implementation, run spec-compliance review, then code-quality review.
7. Update this file after each completed task or important discovery and commit
   the update as a checkpoint.

Suggested new-session prompt:

```text
Continue the Bumpy accurate-port project from PROJECT_STATUS.md in
C:\dev\BUMPY\.worktrees\reverse-engineering-foundation. Verify the checkpoint,
then execute Task 6 from the implementation plan. Update PROJECT_STATUS.md when
Task 6 is complete.
```

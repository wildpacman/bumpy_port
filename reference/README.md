# Reference execution

DOSBox-X runs the original game as a reference, used to check behavior and
visuals when static analysis of the binary is ambiguous.

Run an interactive reference session:

```powershell
& tools/reference/run_reference.ps1
```

The script refuses to start unless the original assets and the pinned DOSBox-X
archive, executable, and version all verify. The game runs from a disposable
copy under `analysis/generated/reference`, so the original files are never
mounted writable. The asset manifest is verified again after DOSBox-X exits.

Bounded automated menu probe:

```powershell
& tools/reference/run_reference.ps1 -VerifyMenu
```

The probe waits for the game's startup video-mode menu, captures the DOSBox-X
window, verifies that DOSBox-X identifies the running program as `BUMPY` at the
pinned 3000 cycles and that the screen is non-trivial, then terminates DOSBox-X.
It never waits for manual input.

## Pinned DOSBox-X archive

Use the standard official `dosbox-x-v2026.06.02` Windows archive. Do not
substitute the `-osfree` archive: its Windows build lacks the DOS shell commands
needed to mount and launch the game.

Download to `tools/vendor/downloads/dosbox-x-mingw64-2026.06.02-portable.zip`:

```text
https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v2026.06.02/dosbox-x-mingw64-2026.06.02-portable.zip
SHA-256 be4faa5edd5980159ed4dfa8c803269beb29a58f02190b6b3ee1a8f52ae57235
```

Every run verifies the archive, extracts a clean ignored installation, and
verifies the selected `dosbox-x.exe` hash and version.

## Using it for recovery

The pinned reference uses VGA, a normal 386 core, and fixed 3000 cycles — keep
these constant while collecting comparable evidence. Open the DOSBox-X debugger
with Alt+Pause. When a runtime observation settles a question (an address, an
input effect, a value), record it in the relevant catalog or `analysis/` note
with its evidence. Captured screenshots are evidence for that run, not
cross-machine golden hashes.

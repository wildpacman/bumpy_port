# Reference execution

Run `tools/reference/run_reference.ps1` for an interactive reference session.
The script refuses to start unless the original assets and the pinned local
DOSBox-X archive, executable, and version all verify.

The game runs from a disposable copy under `analysis/generated/reference`, so
the original files are never mounted writable. The asset manifest is verified
again after DOSBox-X exits.

Run the bounded automated menu probe with:

```powershell
& tools/reference/run_reference.ps1 -VerifyMenu
```

The probe waits for Bumpy's own startup video-mode menu, captures the DOSBox-X
window, verifies that DOSBox-X identifies the running program as `BUMPY` at the
pinned 3000 cycles and that the captured screen is non-trivial, then terminates
DOSBox-X. This rejects a DOSBox-X shell, startup error, or unrelated screen. It
never waits for manual input; the emitted screenshot SHA-256 is evidence for
that individual run rather than a cross-machine golden hash.

The reference runner requires the standard official
`dosbox-x-v2026.06.02` Windows archive. Do not substitute the separately
published `dosbox-x-v2026.06.02-osfree` archive: its Windows build does not
provide the DOS shell commands needed to mount and launch this game.

Download the required archive to
`tools/vendor/downloads/dosbox-x-mingw64-2026.06.02-portable.zip`:

```text
https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v2026.06.02/dosbox-x-mingw64-2026.06.02-portable.zip
SHA-256 be4faa5edd5980159ed4dfa8c803269beb29a58f02190b6b3ee1a8f52ae57235
```

Every run verifies the archive, extracts a clean ignored installation, and
then verifies the selected `dosbox-x.exe` hash and file version.

The pinned reference uses VGA, a normal 386 core, and fixed 3000 cycles. Do not
change these values while collecting comparable evidence. Open the DOSBox-X
debugger with Alt+Pause. Record every breakpoint, observed address, input
sequence, and conclusion in the relevant catalog or report before marking a
function `confirmed`.

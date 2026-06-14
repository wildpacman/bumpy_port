# Reverse-engineering workspace

## Prerequisites

Run the bootstrap from Developer PowerShell for Visual Studio so `cl` is on
`PATH`. The required local command-line tools are:

- Python 3.14.
- CMake 4.2 or newer.
- Git.
- Visual Studio C++ with MSVC.
- A 64-bit JDK 21 for Ghidra.

## Pinned tools

- Ghidra 12.1.2: static analysis and decompilation. Archive SHA-256:
  `b62e81a0390618466c019c60d8c2f796ced2509c4c1aea4a37644a77272cf99d`.
- DOSBox-X 2026.06.02: reference execution and debugger. Archive SHA-256:
  `1c71c6e580a9b675029d0f40f3646bf86f59a4d47dff964974e8f2f6048b51f6`.
- `samrussell/unpacklzexe` commit
  `3a1b8b54e63e7e03181916d40acf7626d5558f6d`: primary LZEXE 0.91
  unpacker.
- Historical Kou Kurizono UNLZEXE C implementation from `mywave82/unlzexe`
  commit `066aac7be3b27813c221d3b03621ad6dfaecd285`: independent LZEXE
  unpack validator.

Run `tools/re/bootstrap_tools.ps1` to check required command-line tools, clone
both unpackers into the ignored `tools/vendor/` directory, and detach them at
their pinned commits. Missing Ghidra and DOSBox-X commands are reported as
warnings because their installation is external to this repository.

Before and after checkout, bootstrap requires both vendor repositories to be
pristine according to `git status --porcelain`. Tracked modifications and
untracked files both stop bootstrap. The script never resets, cleans, deletes,
or changes dirty files; review and remove them manually before running it
again.

## Independent unpack validation

The historical UNLZEXE implementation is independent from the primary Python
implementation derived from disassembly. LZEXE's official companion named
`UPACKEXE` is not a LZEXE decompressor; it unpacks Microsoft's EXEPACK format
and therefore cannot validate this task.

Validation compares normalized execution semantics: load-module bytes, ordered
relocation entries, CS:IP, SS:SP, and minimum/maximum extra paragraphs. Full
EXE files may differ because unpackers choose different header padding.

The pinned primary Python implementation calculates minimum allocation
incorrectly. Its output wrapper normalizes MinBSS and MaxBSS using the
historical UNLZEXE v0.7 formula and the original LZEXE 0.91 stub metadata:
the load-module growth, rounded decompressor size, and nine bookkeeping
paragraphs. The value is derived from the packed source, never copied from the
validator.

## External installation

Download the official pinned archives:

- Ghidra:
  `https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip`
- DOSBox-X:
  `https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v2026.06.02-osfree/dosbox-x-mingw64-dosbox-x-v2026.06.02-osfree-portable.zip`
- Temurin JDK 21.0.11+10:
  `https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.11%2B10/OpenJDK21U-jdk_x64_windows_hotspot_21.0.11_10.zip`
  with SHA-256
  `d3625e7cadf23787ea540229544b6e2ab494b3b54da1801879e583e1dfee0a64`.

Verify downloaded archives before extraction:

```powershell
New-Item -ItemType Directory -Force tools/vendor/downloads | Out-Null
Invoke-WebRequest `
  https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip `
  -OutFile tools/vendor/downloads/ghidra_12.1.2_PUBLIC_20260605.zip
Invoke-WebRequest `
  https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v2026.06.02-osfree/dosbox-x-mingw64-dosbox-x-v2026.06.02-osfree-portable.zip `
  -OutFile tools/vendor/downloads/dosbox-x-mingw64-dosbox-x-v2026.06.02-osfree-portable.zip
& tools/re/bootstrap_tools.ps1
```

The bootstrap fails on a hash mismatch. It warns when either archive is absent
because downloading and installing these tools remains external.

Only after this verification succeeds, extract the archives. Task 5 uses
ignored local installations under `tools/vendor`; it does not install
system-wide or modify the user `PATH`.

After installation, verify the external tools:

```powershell
& tools/re/bootstrap_tools.ps1
Get-Command ghidraRun,analyzeHeadless,dosbox-x
```

## Artifact policy

Downloaded tools and generated binaries belong in ignored directories,
including `tools/vendor/` and `analysis/generated/`. Reports, catalogs,
scripts, and conclusions belong in git.

## Reproducible Ghidra analysis

Run the complete Task 5 verification from any directory:

```powershell
& C:\dev\BUMPY\.worktrees\reverse-engineering-foundation\tools\re\run_ghidra_analysis.ps1
```

The wrapper does not modify the system installation or user `PATH`. It:

- verifies the pinned Ghidra 12.1.2 and Temurin JDK 21.0.11 archives;
- compares key installed files against bytes in those verified archives;
- verifies the bundled PyGhidra 3.1.0 wheel and force-installs it into an
  ignored local virtual environment;
- runs asset verification and independently validated unpacking;
- creates two separate clean ignored Ghidra projects using `MzLoader` and
  `x86:LE:16:Real Mode`;
- requires identical raw function-discovery hashes before atomically merging
  discoveries into the curated function catalog;
- writes stable address mapping and analysis-report artifacts.

Ghidra rejects project paths with a component beginning with `.`. During a run,
the wrapper creates a checked junction at `C:\dev\BUMPY_GHIDRA_ALIAS`, verifies
its target before use/removal, and removes it afterward. Project data remains
physically inside ignored `analysis/generated/`.

`analysis/catalog/functions.csv` is the curated catalog. Re-analysis preserves
existing `status`, `evidence`, `cpp_symbol`, and names by address. New addresses
are added as `unknown`. Rows absent from the latest discovery are retained and
marked in evidence as `not discovered in latest analysis`.

`analysis/catalog/function_addresses.csv` is generated one-to-one from the
curated catalog. For a segmented real-mode address `segment:offset`:

- `linear_address = segment * 16 + offset`;
- `image_offset = linear_address - 0x10000`.

`0x10000` is the Ghidra MZ load-module base represented by `1000:0000`.
Addresses below this base are rejected rather than assigned a guessed offset.

Both clean analyses currently emit the same warning exactly twice:

```text
Decompiling 1000:35a5, pcode error at 1000:84d7: Unable to resolve constructor at 1000:84d7
```

The wrapper allowlists only this exact warning and records its count. Confirmed
evidence is limited to this: the warning is present while Ghidra reports
successful headless analysis and import. No broader effect has been
established.

## Evidence states

- `unknown`: exported but not investigated.
- `hypothesis`: named from evidence that still needs an independent check.
- `confirmed`: validated by static analysis plus reference execution or an
  independent implementation.

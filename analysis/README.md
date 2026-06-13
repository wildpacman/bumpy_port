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
- Official LZEXE 0.91 `UPACKEXE.EXE`: independent unpack validation under
  DOSBox-X.

Run `tools/re/bootstrap_tools.ps1` to check required command-line tools, clone
`unpacklzexe` into the ignored `tools/vendor/` directory, and detach it at the
pinned commit. Missing Ghidra and DOSBox-X commands are reported as warnings
because their installation is external to this repository.

After checkout, bootstrap requires `tools/vendor/unpacklzexe` to be pristine
according to `git status --porcelain`. Tracked modifications and untracked
files both stop bootstrap. The script never resets, cleans, deletes, or changes
dirty files; review and remove them manually before running it again.

## External installation

Download the official pinned archives:

- Ghidra:
  `https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip`
- DOSBox-X:
  `https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v2026.06.02-osfree/dosbox-x-mingw64-dosbox-x-v2026.06.02-osfree-portable.zip`

Verify both downloads before extraction:

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

Only after this verification succeeds, extract both archives outside the
repository. Install a 64-bit JDK 21 for Ghidra. Add the Ghidra directory, its
`support/` directory, and the DOSBox-X directory to the user `PATH`. Do not
place installations in tracked repository directories.

After installation, verify the external tools:

```powershell
& tools/re/bootstrap_tools.ps1
Get-Command ghidraRun,analyzeHeadless,dosbox-x
```

## Artifact policy

Downloaded tools and generated binaries belong in ignored directories,
including `tools/vendor/` and `analysis/generated/`. Reports, catalogs,
scripts, and conclusions belong in git.

## Evidence states

- `unknown`: exported but not investigated.
- `hypothesis`: named from evidence that still needs an independent check.
- `confirmed`: validated by static analysis plus reference execution or an
  independent implementation.

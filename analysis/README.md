# Reverse-engineering workspace

## Pinned tools

- Ghidra 12.1.2: static analysis and decompilation.
- DOSBox-X 2026.06.02: reference execution and debugger.
- `samrussell/unpacklzexe` commit
  `3a1b8b54e63e7e03181916d40acf7626d5558f6d`: primary LZEXE 0.91
  unpacker.
- Official LZEXE 0.91 `UPACKEXE.EXE`: independent unpack validation under
  DOSBox-X.

Run `tools/re/bootstrap_tools.ps1` to check required command-line tools, clone
`unpacklzexe` into the ignored `tools/vendor/` directory, and detach it at the
pinned commit. Missing Ghidra and DOSBox-X commands are reported as warnings
because their installation is external to this repository.

## External installation

Download the official pinned archives:

- Ghidra:
  `https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip`
- DOSBox-X:
  `https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v2026.06.02-osfree/dosbox-x-mingw64-dosbox-x-v2026.06.02-osfree-portable.zip`

Extract both outside the repository. Install a 64-bit JDK 21 for Ghidra. Add
the Ghidra directory, its `support/` directory, and the DOSBox-X directory to
the user `PATH`. Do not place downloaded archives or installations in tracked
repository directories.

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

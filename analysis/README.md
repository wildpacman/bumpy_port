# Reverse-engineering workspace

Recovery is grounded in the binary and the original files. Confirmed facts and
specifications are tracked here; large generated artifacts are not.

## Contents

- `RESOURCE_PIPELINE.md` — recovered resource/loader map (segment mapping,
  resource table, open/read/close path, draw entry points).
- `catalog/functions.csv`, `catalog/globals.csv` — function/global catalogs by
  segmented address with a recovery `status` and evidence.
- `catalog/function_addresses.csv` — address mapping derived from the catalog.
- `ghidra_scripts/` — Ghidra export scripts.
- `specs/` — byte-level resource format specifications (added per stage).
- `generated/` — **ignored**: unpacked exe, decompilation output, downloaded
  tools, reference captures, Ghidra projects.

## Decompilation

The unpacked image is `generated/BUMPY.UNPACKED.EXE`. Regenerate the readable
decompilation with:

```powershell
& tools/re/decompile_loader.ps1
```

This is a one-pass PyGhidra run (`MzLoader`, `x86:LE:16:Real Mode`) that reuses
the already-downloaded Ghidra 12.1.2 + JDK and writes to `generated/decomp/`:

- `all_functions.c` — every function as readable C;
- `strings.txt` — defined strings with addresses;
- `filename_xrefs.txt`, `index.txt` — references and a function index.

Read functions there and correlate with the strings, the resource table, and the
raw bytes of the original files.

## Tools

- **Ghidra 12.1.2** — disassembly and decompilation (downloaded under
  `generated/`; not committed).
- **DOSBox-X** — reference execution and debugger, used to check behavior when
  static analysis is ambiguous (see `reference/README.md`).
- **LZEXE unpack** — `tools/re/unpack_bumpy.py` produces the unpacked image.

## Evidence states

- `unknown` — exported but not investigated.
- `hypothesis` — named from evidence that still needs a check.
- `confirmed` — validated by static analysis plus a runtime/visual check or by
  decoding every supplied file of the format.

## Segment mapping

Ghidra loads the image at segment `0x1000`. The data segment is the
load-relative `0x103b`, shown by Ghidra as `0x203b` (`+0x1000`). For a file
offset `F` (≥ `0x1090`): linear `= 0x10000 + (F − 0x1090)`. For a segmented
real-mode address `segment:offset`: `linear = segment*16 + offset`,
`image_offset = linear − 0x10000`.

## Artifact policy

Reports, catalogs, scripts, specs, and conclusions belong in git. Downloaded
tools and generated binaries belong under ignored `tools/vendor/` and
`analysis/generated/`.

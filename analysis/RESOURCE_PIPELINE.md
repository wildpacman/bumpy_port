# Bumpy resource/loader pipeline (recovered from BUMPY.UNPACKED.EXE)

Recovered 2026-06-19 from the Ghidra decompilation
(`analysis/generated/decomp/all_functions.c`). Addresses are Ghidra segmented
(`1000:xxxx` code, `203b:xxxx` data). Confidence is marked per the evidence
ladder.

## Segment mapping (Confirmed)

- Ghidra load base = segment `0x1000`. The data segment Ghidra calls `0x203b`
  is stored in the binary as the **load-relative** segment `0x103b`
  (`0x203b - 0x1000`). Code that builds far pointers to data uses `0x103b`.
- File offset `F` (>= `0x1090`) ⇒ Ghidra linear `0x10000 + (F - 0x1090)`.
- Data segment `203b:off` ⇒ file offset `0x203b*16 + off - 0x10000 + 0x1090`.

## Build / toolchain (Confirmed)

- Compiled with **Turbo C++ 1990 (Borland)**, 16-bit real mode, large/far-data
  model. DOS calls appear as `swi(0x21)` in the decompilation.

## Resource table (Confirmed)

Two tables of **10-byte entries**, base set via `FUN_1000_7307(off, seg)`:

| Field | Offset | Width | Meaning |
|---|---|---|---|
| name_off | 0 | u16 | filename string offset (in seg `0x103b`) |
| name_seg | 2 | u16 | `0x103b` (load-relative data segment) |
| disk | 4 | u8 | disk letter: `'a'`/`'b'` (prompt to swap), `'z'`=no swap |
| (rest) | 5 | 5 B | not yet decoded (Hypothesis: handle/state cache) |

- Menu/common table @ `203b:0928`: `BUMSPJEU.BIN, MASKBUMP.VEC, BUMPRESE.VEC,
  SCORE.VEC, BUMPY.BNK, BUMPY.MID, <2 'z' markers>, MONDE1..4.VEC` (indices 0..11).
- Level table @ `203b:0090`: `D?.PAV, D?.DEC, SPRITE.BIN, GRILLE.VEC,
  BUMPYOBJ.VEC, BUMPYSPR.BIN, GRILLOBJ.VEC, ...` (the `?` digit is patched at
  runtime with the level number, e.g. `FUN_1000_2d14` writes `level+'0'`).
- Filenames are stored 8.3, space-padded (`SPRITE  .BIN`).

## Load pipeline (Confirmed)

| Function | Role |
|---|---|
| `FUN_1000_7307(off,seg)` | select active resource table base (`DAT_203b_a1b4`) |
| `FUN_1000_736f(index,mode)` | open resource `index` → DOS handle; handles disk-swap ("INSERT THE OTHER DISK") |
| `FUN_1000_a21c(nameoff,nameseg,flags,mode)` | low-level open wrapper (DOS open) |
| `FUN_1000_7235(readfn,handle,dstoff,dstseg,size_lo,size_hi)` | read `size` bytes from handle into far buffer in <=64000-byte chunks |
| `FUN_1000_745e(handle,dstoff,dstseg,size_lo,size_hi)` | read-raw via `FUN_1000_7235(0xa3ae,...)` |
| `FUN_1000_7319(handle)` | close handle |

**Key fact:** `.VEC`/`.BIN` resources are loaded **raw** into memory. The
file's own bytes are NOT decoded at load time.

## Draw / decode (Hypothesis — NEXT to read)

Pixel interpretation + palette happen at draw time, not load time:

- `FUN_1000_7b5a(srcoff,srcseg,size,w,h)` — blit/interpret a loaded buffer to
  the VGA framebuffer. **This is where the `.VEC` pixel format lives.**
- `FUN_1000_7b93(...)` + `FUN_1000_08d1` — palette setup (builds a 16-entry RGB
  table with `<<3` shifts ⇒ 6-bit VGA DAC values). 16-color path seen; a 256
  path likely exists (`VGA256` menu option).
- `FUN_1000_2ef8` — **title-screen path** (loads + draws `TITRE.VEC`); good
  entry point for the menu milestone.
- `FUN_1000_0c18` — main gameplay loop; `FUN_1000_2d14` — per-level init+draw.

## Next step (Stage 1)

Read `FUN_1000_7b5a` and `FUN_1000_7b93` to recover the exact `.VEC` pixel
encoding and palette, then transcribe to `src/resources/vec` + render `TITRE.VEC`.

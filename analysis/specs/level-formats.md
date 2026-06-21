# Level resource formats (Stage 3)

Recovered 2026-06-21. Evidence levels per the project's ladder
(Structural / Hypothesis / Confirmed). Offsets are zero-based.

Per-level init+draw is `FUN_1000_2d14` (`1000:2d14`). It patches the current
level digit (`DAT_203b_79b2`) into the `?.PAV`/`?.DEC`/`?.BUM` filenames in the
level resource table at `203b:0090` (entries 0, 1, 8), loads each file **raw**,
then passes each through `FUN_1000_7b5a` — the same layered-VEC decoder used for
`TITRE.VEC`. So all three level blobs are **layered-VEC containers**.

## Container (Confirmed)

`D?.PAV`, `D?.DEC`, `D?.BUM` use the 12-byte layered-VEC header documented in
[menu-resource-formats.md](menu-resource-formats.md) and decode with the existing
`src/resources/vec` decoder (method 4 marker-RLE + method 12 marker-mask, chained
across layers). Verified by decoding every supplied file:

| File | decoded size | layers |
|---|---:|---|
| `D{1,2,4..9}.PAV` | 30726 | usually 3 (method 4 → 12 → 12) |
| `D{1,2,3,6,8}.DEC` | 12182 | 1–3 |
| `D{4,5,7,9}.DEC` | 9746 | 1–3 |
| `D{1,2,3,7,8}.BUM` | 2912 | 1–3 |
| `D{4,5}.BUM` | 2330 | 1–3 |

The decoded sizes are **fixed per type**; DEC and BUM each come in two sizes that
co-vary (12182↔2912 vs 9746↔2330), i.e. two playfield dimensions.

### Exceptions (Confirmed)

- `D3.PAV` is a 0-byte file (level 3 ships no PAV).
- `D6.BUM` (2912 B) and `D9.BUM` (2330 B) are **stored already-decoded** (raw):
  their first bytes are not a valid layer header, and their file sizes equal the
  decoded BUM size of their group. Their content starts with the same `0x100e` /
  `0x100f` BUM header as the decoded BUMs. A port must therefore try VEC-decode
  and fall back to treating the file as raw decoded BUM data when the header is
  not a valid VEC layer header.

Inspect any of these with the dev flags added to `bumpy_port`:

```powershell
bumpy_port.exe --decode-vec D1.PAV out.bin           # decode + print layer chain
bumpy_port.exe --render-screen MONDE1.VEC out.bmp    # 320x200 screen-format VEC
bumpy_port.exe --render-pav D1.PAV MONDE1.VEC out.bmp planeseq 320 192 6
```

## D?.PAV — playfield object sheet (Confirmed)

6-byte header followed by a **320×192 plane-sequential 4-plane VGA image**
(4 × 7680 bytes = 30720; 6 + 30720 = 30726). Same plane order and MSB-first bit
order as the screen format, but **plane-sequential** (all of plane 0, then 1, 2,
3), not row-interleaved. Colour index 0 is transparent.

Confirmed by deplaning `D1.PAV` and viewing by eye: the world-1 object sheet —
teddy bear, hot-air balloon, smaller bear, candies, presents, stars, a drum —
matching the MONDE1 teddy-bear theme. The draw routine `FUN_1000_0a90` builds a
blit command from `PAV_buffer + 6` (hence the 6-byte header) and stamps tiles
from this sheet onto the playfield, indexed by the level grid.

PAV has no embedded palette; objects render in natural tones under the matching
`MONDE?.VEC` palette. Whether the level palette is the MONDE palette or comes
from DEC is not yet confirmed (Hypothesis: shared with MONDE).

## MONDE?.VEC — per-world background screen (Confirmed)

The 9 `MONDE?.VEC` files decode to the 32099-byte **screen format** (99-byte
header with the 16-colour VGA palette at offset 51, then four 8000-byte
plane-sequential bit-planes) — identical to `TITRE.VEC`. Rendered by eye:
full-colour per-world backgrounds (MONDE1 teddy bears + balloon; MONDE2 carousel
with lion and elephant) overlaid with a **4×5 grid of blue ring "nodes"** — the
world/level board. `BUMPRESE.VEC` and `DESSFIN.VEC` are also screen-format.

`MONDE?.VEC` is the playfield backdrop; `D?.PAV` objects are drawn on top.

## D?.BUM — object/tile grid (Hypothesis)

2-byte header (`0x100e`, or `0x100f` for D9) then a grid of **1-byte cells**.
Cell values are small object/tile indices (observed 0–0x2e); 0 is empty. These
index the PAV object sheet for placement. Two grid sizes (2910 / 2328 cells after
the header) match the two DEC sizes.

Exact grid width/height is **not yet confirmed** — to be read from the gameplay
grid consumer of buffer `203b:75de` rather than inferred from the data (design
patterns create misleading periodicity). The PAV draw `FUN_1000_0a90` uses a
related structure `_DAT_203b_6bca` with row stride `0x27` (39) = 13 three-byte
records per row.

## D?.DEC — decor / object-placement records + palette (Hypothesis)

2-byte header (`0x000e` = 14) then fixed records. Early bytes include
8-bit-valued groups that look like a palette (`f8 a8 bc d2`, `f8 e6 e6 f8`),
followed by a run of **3-byte records** (`VV 00 00`, `VV` = object/tile index in
the same range as BUM cells). The 3-byte stride matches `FUN_1000_0a90`'s
`(row>>1)*3` indexing. Record count, field meanings, and the palette layout are
not yet confirmed.

## Open questions / next blockers

1. Confirm the BUM grid dimensions and DEC record layout from the gameplay
   consumers (`FUN_1000_3467`, the per-frame draw chain in `FUN_1000_0c18`, and
   buffer `203b:75de`/`6bca`).
2. Confirm the level palette source (MONDE vs DEC).
3. Compose a static level (MONDE background + PAV objects placed by the grid) in
   the port and compare to the original by eye.
4. Then: physics, collision, objects, win/loss, return to menu.

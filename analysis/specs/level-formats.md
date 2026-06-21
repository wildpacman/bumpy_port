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

## Both files are N parallel per-board records (Confirmed)

`FUN_1000_0416` allocates the level buffers and `FUN_1000_32b0` activates the
current board. The decoded DEC and BUM are each a **2-byte file header followed
by N fixed-size board records**, where N is the level's board count:

- DEC board = `0x32c` (812) bytes. `6bd2 = DEC_buffer + 2`;
  `6bca = 6bd2 + board * 0x32c`.
- BUM board = `0xc2` (194) bytes. `6bf2 = BUM_buffer + 2`;
  `75d0 = 6bf2 + board * 0xc2`.

Board count divides exactly and matches between the two files:

| variant | DEC size | (DEC−2)/812 | BUM size | (BUM−2)/194 |
|---|---:|---:|---:|---:|
| large | 12182 | **15** | 2912 | **15** |
| small | 9746 | **12** | 2330 | **12** |

So a level has **15 or 12 boards** (the two size variants). `D6.BUM`/`D9.BUM`
ship raw but with the same 2912/2330 sizes, i.e. the same 15/12-board layout.

## D?.DEC — static tile grid per board (Confirmed)

2-byte file header, then 15 (or 12) board records of 812 bytes. Each board:

| Region | Offset | Size | Meaning |
|---|---:|---:|---|
| board header | `0x00` | 32 | 16 words of board metadata (read as `6bca + n*2`; e.g. word `0x1c` gated in `FUN_1000_2cf*`). |
| cell grid | `0x20` | 780 | **20 columns × 13 rows**, 3 bytes/cell, column-major. |

The renderer `FUN_1000_2a0a` iterates `x = 0..19` (col, stride `0x27`=39) and
`y = 0..12` (row, via `iVar3 = 0..24` step 2, `iVar3>>1` × 3). For each cell it
draws the base tile (`FUN_1000_0b88`) and, if **cell byte 0 ≠ 0**, the object
(`FUN_1000_0a90`):

```text
cell_addr = DEC + 2 + board*0x32c + 0x20 + x*0x27 + y*3
obj = cell[0]
```

Cell byte 0 semantics (`FUN_1000_0a90`):
- `0` — empty (object not drawn; base tile only).
- `1..0xf0` — single object; PAV source tile = `((obj-1) % 20, ((obj-1)/20)*2)`.
- `≥ 0xf1` — **stacked object**: draws `(-obj - 5)` tiles using the extra cell
  bytes (`cell[1]`, `cell[2]`, …) as the stacked object indices. This is why
  cells are 3 bytes wide.

## D?.BUM — dynamic per-board entities ("bumpers") (Confirmed)

2-byte file header, then 15 (or 12) board records of 194 bytes. On board
activation `FUN_1000_32b0` copies each record into the active-board working
buffer `203b:a0e4`:

| Region | Offset | Size | Meaning |
|---|---:|---:|---|
| table A | `0x00` | 48 | per-entity bytes (copied) |
| table B | `0x30` | 48 | per-entity bytes (copied) |
| table C | `0x60` | 48 | per-entity bytes (copied) |
| params | `0x90` | 6 | board entity params (copied verbatim) |
| (rest) | `0x96` | 44 | remainder of the 194-byte record |

The three 48-byte tables are parallel per-entity arrays (Hypothesis: up to 16
moving objects/bumpers × 3 bytes, or x/y/type columns). Exact per-entity field
meanings are the next thing to pin down when implementing entity spawning.

## Open questions / next blockers

1. BUM entity table field semantics (the three 48-byte arrays + 6 params).
2. Level palette source for gameplay (PAV has none; MONDE is the world-map
   screen — confirm whether gameplay reuses a per-world palette or a global one).
   The playfield background is per-cell base tiles (`FUN_1000_0b88`), not MONDE.
3. PAV sheet tiling: confirm source tile pixel size from the blitter (`1cec`);
   `FUN_1000_0a90` addresses it as 20 columns with row step ×2.
4. Compose a static board (base tiles + DEC objects) in the port; compare by eye.
5. Then: physics, collision, entities, win/loss, return to menu.

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

Board count divides exactly in each file:

| variant | DEC size | (DEC−2)/812 | BUM size | (BUM−2)/194 |
|---|---:|---:|---:|---:|
| large | 12182 | **15** | 2912 | **15** |
| small | 9746 | **12** | 2330 | **12** |

So a level has **15 or 12 tile-boards** (the two DEC size variants). `D6.BUM`/
`D9.BUM` ship raw but with the same 2912/2330 sizes.

**The two counts do not always agree** (verified by file size, not assumed):
`D7.DEC` is small (9746 → **12** tile-boards) while `D7.BUM` is large (2912 →
**15** entity-boards). So the port keeps the DEC and BUM board counts independent
rather than cross-validating them; the DEC count is the playfield board count.

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
- `0` — empty (object not drawn; only the base-tile clear, see below).
- `1..0xf0` — single object; PAV source tile = `((obj-1) % 20, (obj-1)/20)`, a
  16×16 px tile (the `×2` row factor in the disassembly cancels with the 8 px
  blit height-unit; see "Blit geometry" below).
- `≥ 0xf1` — **stacked object**: stamps `(uint8_t)(-obj - 5)` tiles whose indices
  are the following cell bytes (`cell[1]`, `cell[2]`, …, spilling into later cells
  for longer stacks), all at the same cell. This is why cells are 3 bytes wide.

### Base tile = flat colour-0 clear, not a floor sprite (Confirmed, static)

`FUN_1000_0b88`'s per-cell "base tile" is **not** an indexed floor tile. Recovered
from the overlay planar path (`1ab9:0179` → mode handler `1ab9:002b`, see
"Base-Tile Blit Recovery" below): command mode `+0x1c == 0` reads no bitmap
source — it fills the destination planes with a 4-pixel pattern built from command
bytes `+0x22..+0x25`, and `FUN_1000_0b88` zeroes those bytes. So each cell is
cleared to **colour index 0**. The port reproduces it as a full-frame index-0
clear; the visible "floor" cross-hatch is actually the dense `0x63` PAV *objects*,
not the base pass.

### Blit geometry (Confirmed, static)

The overlay planar path maps command width-units to **16 px** (`×2` planar bytes)
and height-units to **8 px** (`×8` scanlines). A playfield cell has dest
`+0x1e = 1` (16 px wide) and `+0x20 = 2` (16 px tall); the clipped bottom row uses
`+0x20 = 1` (16×8). The PAV sheet (320×192) is therefore **20 columns × 12 rows of
16×16 tiles**, addressed by object index as above.

## D?.BUM — dynamic per-board entities ("bumpers") (Confirmed)

2-byte file header, then 15 (or 12) board records of 194 bytes. On board
activation `FUN_1000_32b0` copies each record into the active-board working
buffer `203b:a0e4`:

| Region | Offset | Size | Meaning |
|---|---:|---:|---|
| layer A | `0x00` | 48 | 8×6 entity grid — pegs/bumpers |
| layer B | `0x30` | 48 | 8×6 entity grid — second layer (col 7 unused) |
| layer C | `0x60` | 48 | 8×6 entity grid — collectibles |
| params | `0x90` | 6 | board entity params (copied verbatim) |
| (rest) | `0x96` | 44 | remainder of the 194-byte record (byte `0x96` used) |

### Entity layers = three 8-column × 6-row grids (Confirmed)

The spawn/draw routine `FUN_1000_2a78` (called right after `FUN_1000_32b0` on
board activation) iterates the working buffer as a grid, **not** as packed
per-entity records:

```c
for (row = 0; row < 6; row++)
  for (col = 0; col < 8; col++) {
    cell = row*8 + col;
    a = buf[0x00 + cell];  if (a)            draw_layer_a(cell, a);   // FUN_165e/1a67
    b = buf[0x30 + cell];  if (b && col!=7)  draw_layer_b(cell, b);   // FUN_17c7/1b2b
    c = buf[0x60 + cell];  if (c)            draw_layer_c(cell, c);   // FUN_942a
  }
```

So each of the three 48-byte tables is an **8×6 grid** (cell = `row*8 + col`),
and the three are independent **layers** drawn by three distinct paths — not
x/y/type columns of one entity list. A non-zero cell means "entity present"; its
value selects the sprite/type:

- **Layer A** (pegs/bumpers): value indexes a sprite descriptor table at
  `0x3d3a`/`0x3d6a`. In `D1` board 0 it is the fixed `0/1` peg field (4 pegs per
  row in rows 0–4, a solid bottom row, column 7 always empty) — 27 pegs.
- **Layer B**: indexes a descriptor table at `0x4086`/`0x40a6`; column 7 is never
  drawn. Empty on `D1` board 0.
- **Layer C** (collectibles): sprite id = `value + 0x179`; screen position from a
  coordinate table (below). In `D1` board 0: codes `0x1b,0x03,0x17,0x29,0x0f,0x0e`
  at six scattered cells.

### Cell → screen position (Confirmed, extracted)

Layer C reads its position from a data-segment table at `DS:0x274` indexed by
`(col*2 + row*0x10)`; the table is a contiguous 8×6 array of `(x,y)` word pairs.
Extracted from `BUMPY.UNPACKED.EXE` (file `0x1090 + 0x103b*0x10 + 0x274 =
0x116b4`):

| | col 0 | col 1 | … | col 6 | col 7 |
|---|---:|---:|---|---:|---:|
| x | 8 | 48 | (+40) | 248 | 32 (spare) |
| y (per row) | 8, 40, 72, 104, 136, 168 | | | | |

So columns 0–6 sit at `x = 8 + col*40` and rows at `y = 8 + row*32`; column 7 is
a spare slot at `x = 32` that layers A/B never use. This is `bum_cell_position`
in `src/resources/level_resources`. (Layers A/B store the cell index for
`FUN_165e`/`FUN_17c7`, presumed to use the same grid — **Hypothesis** that those
paths share this table; not byte-verified.)

### Board params `0x90..0x95` + byte `0x96` (Confirmed structure)

Read by `FUN_1000_2a78`. `D1` board 0 = `29 2c 06 00 09 00`:

- `0x90`, `0x91` — **1-based grid cell indices** (decremented when non-zero):
  `0x29`→cell 40, `0x2c`→cell 43 (both bottom-row). Likely Bumpy's start /
  an entry/exit cell.
- `0x92` (`→ a0cf`), `0x94` (`→ 8562`), `0x95` (`→ 7920`) — small per-board
  counts/flags (exact roles not yet pinned).
- `0x93` (`→ 8571`, decremented) — a cell index; `0xff` sentinel when `0x00`.
- byte `0x96` indexes a word table at `0x2546` → `a0de` (a per-board value).

### Decoder + inspection (Implemented)

`src/resources/level_resources` decodes a board into `BumEntities` (the three
8×6 layers + 6 params), validated against the `D1` values above
(`tests/cpp/level_resources_test.cpp`). `overlay_bum_entities`
(`src/video/board_renderer`) draws a marker per occupied cell at its faithful
position for by-eye checking: `--render-board 1 MONDE1.VEC 0 out.bmp entities`
(`analysis/generated/board_L1_B0_entities.png`). The markers confirm the grid and
positions; they are **not** the original sprites — see the blocker below.

## Resolved (Stage 3 static board)

- **PAV tile size** — 16×16 px, sheet 20×12 tiles. Confirmed from the blit
  geometry (width-unit 16 px, height-unit 8 px) and by eye: large objects
  (balloon, bear) compose seamlessly across cells.
- **Base tile** — a flat colour-index-0 clear, not a floor sprite (above).
- **Static board** — composed in the port (`src/resources/level_resources`,
  `src/video/board_renderer`, `--render-board`): base index-0 clear + DEC-placed
  PAV objects (single + stacked) on the per-world palette. Verified by eye against
  the original art (`analysis/generated/board_L1_B0.bmp`).
- **Gameplay palette** — the per-world `MONDE?.VEC` palette renders the objects in
  correct natural tones (Hypothesis upgraded to confirmed-by-eye). `MONDE?.VEC`
  itself is the world-select map screen, not the in-level backdrop.
- **BUM entity layout** — three 8×6 entity layers + 6 params, with the faithful
  cell→pixel coordinate table (above). Decoded into `BumEntities` and validated
  against `D1` data and by eye via the marker overlay.

## Entity sprite bank (Recovered + Implemented)

The entity sprites all come from the **uncompressed** `BUMSPJEU.BIN` bank, drawn
through `FUN_1000_2a78` → per layer → `DAT_8884 = [x, y, frame_index]` →
`FUN_1000_942a` → `1cec:31b7` → `1cec:2ded`. The bank's **master frame table** is
addressed directly by frame index: the existing `decode_sprite_frame(bumspjeu,
idx)` already does it (`be32(bytes, idx*4) + 0x800` → 12-byte header + planar
pixels). The selection chains, recovered from the static descriptor tables in
`BUMPY.UNPACKED.EXE` (data segment `0x103b`; file offset = `0x11440 + DS_offset`):

- **Layer A** (pegs/bumpers): `value → 0x3d3a[value] = sprite_index → record at
  DS:0x37be + (sprite_index-1)*4 = {count, frame_index}`. The peg code `1` → frame
  `0x40` (a 32×6 bumper). Draw position is `DS:0xf4` (`x = col*40`, `y = 24 +
  row*32`), with `count` added to `y` (`DAT_8884[1]` in `FUN_1000_165e`).
- **Layer B**: `value → 0x4086[value] → record at DS:0x3ad2`. Its frame indices are
  small and reference a bank region not yet pinned (it is empty on `D1` board 0);
  decode defensively. Column 7 is never drawn.
- **Layer C** (collectibles): `frame_index = value + 0x179`; position from `DS:0x274`
  (`x = 8 + col*40`, `y = 8 + row*32`). The `D1` board-0 codes
  `0x1b,0x03,0x17,0x29,0x0f,0x0e` decode to a flag, popsicle, pizza, sundae, etc.
  (16×16 each), confirmed by eye.

Implemented in `src/resources/entity_sprites` (recovered tables + resolution) and
`draw_bum_entities` in `src/video/board_renderer`; wired into `--render-board
<level> <MONDE.VEC> <board> out.bmp sprites` and the live SDL board. Validated on
`D1` board 0 (27 pegs + 6 collectibles, 0 skipped) in
`tests/cpp/entity_sprites_test.cpp` and by eye
(`analysis/generated/board_L1_B0_sprites.png`).

The **compressed** sprite-frame path (flags `0x40`/`0x20`, `1cec:2ded`) is fully
recovered from ground-truth disassembly but **unused by the supplied assets**
(`BUMSPJEU` is 404+ frames all flags `0x0003`; the compressed `BUMPYSPR.BIN` /
`SPRITE.BIN` are not shipped). See `analysis/specs/menu-resource-formats.md`.

## Open questions / next blockers
1. Per-world palette plumbing: confirm which resource the live game installs as
   the gameplay palette. The entity sprites and PAV objects currently render under
   the `MONDE?.VEC` map palette (world 1 = brown-tinted); the in-level gameplay
   palette is likely a different resource and is not yet traced. This is the main
   reason the rendered board looks monochrome-brown rather than colourful.
2. Layer B sprite source: its `0x3ad2` records yield small frame indices that do
   not resolve in the same `BUMSPJEU` master table region as layers A/C; the bank
   region / base for layer B is not yet pinned (it is empty on `D1` board 0).
3. Param roles `0x92/0x94/0x95` and byte `0x96`'s `0x2546` table — structure is
   known; exact gameplay meaning (counts vs flags) is not yet pinned.
4. Then: physics, collision, win/loss. The menu → level → menu shell is already
   wired (`src/game/app`, `src/platform_sdl3`): confirming "start" shows the
   static board in-window and Escape returns to the menu; what remains is the live
   board loop and a win/loss return path.

## Base-Tile Blit Recovery

Evidence source: `analysis/generated/BUMPY.UNPACKED.EXE`, SHA-256
`3ff2f60b474dc04b1de7c69cf3764b95e31967b74a00f755d231ddd3235adbe0`.
The MZ header has `e_cparhdr = 0x0109`, so the load module starts at file
offset `0x1090`. Ghidra's displayed segments are biased by `+0x1000`; for
example runtime `0ab9:0179` is shown as `1ab9:0179`, and maps to file
`0x1090 + 0xab90 + 0x0179 = 0xbd99`.

Classification note: this section uses the source-port evidence ladder. Because
this task is static-only, the new conclusions below are **Structural** unless
explicitly marked **Hypothesis**; they are byte-derived but not runtime-confirmed.

### Base caller path (Structural)

`FUN_1000_7b4a` at `1000:7b4a` (file `0x8bda`) is:

```asm
1000:7b4a  55                 push bp
1000:7b4b  8b ec              mov  bp,sp
1000:7b4d  8b 56 06           mov  dx,[bp+6]
1000:7b50  8b 46 04           mov  ax,[bp+4]
1000:7b53  5d                 pop  bp
1000:7b54  9a 79 01 b9 0a     call far 0ab9:0179
```

So the per-cell base draw issued by `FUN_1000_0b88` goes directly to runtime
`0ab9:0179` / Ghidra `1ab9:0179` (file `0xbd99`). It does not go through
`1cec:31b7` on this path.

At `1ab9:0179` (file `0xbd99`) the overlay sets full-screen bounds and selects
the destination-as-slot mode:

```asm
1ab9:0179  8e db              mov ds,bx          ; bx = 103b
1ab9:017d  8e c2              mov es,dx
1ab9:017f  8b f8              mov di,ax          ; ES:DI = blit command
1ab9:0181  26 c7 45 18 14 00  mov word es:[di+18],0014
1ab9:0187  26 c7 45 1a 19 00  mov word es:[di+1a],0019
1ab9:018d  c6 06 1f 54 02     mov byte [541f],02
1ab9:0192  c6 06 20 54 01     mov byte [5420],01
1ab9:0197  8b 1e 1d 54        mov bx,[541d]
1ab9:019d  ff 97 da 4d        call word [bx+4dda]
```

`103b:4dda` (file `0x1621a`) is a video-driver selector table. Entries 1 and 2
both point at `1ab9:0000` (file `0xbc20`), the planar VGA/EGA-like path. Entry
0 is `1ab9:01ae`, a `ret`; therefore `103b:541d` is a video-driver selector,
not the blit command mode.

### Source of `+0x1c == 0` base mode (Structural)

In the active planar path, `1ab9:0000` (file `0xbc20`) reads the command mode,
calls the common geometry mapper, and dispatches through `103b:4dcc`:

```asm
1ab9:0000  be e6 4d           mov si,4de6
1ab9:0003  e8 47 06           call 064d          ; writes VGA GC/SEQ setup
1ab9:0006  b9 02 00           mov cx,0002
1ab9:0009  e8 1b 04           call 0427          ; derive byte geometry
1ab9:000c  26 8b 5d 1c        mov bx,es:[di+1c]  ; command mode
1ab9:0014  e8 16 05           call 052d          ; common pointer mapper
1ab9:0018  1f                 pop ds             ; DS = command segment
1ab9:0019  8b f3              mov si,bx          ; SI = command offset
1ab9:0021  d1 e3              shl bx,1
1ab9:0023  8b 87 cc 4d        mov ax,[bx+4dcc]   ; mode table
1ab9:0028  ff d0              call ax
```

For `+0x1c == 0`, `103b:4dcc[0] = 0x002b` (file `0x1620c`), so the mode handler
is `1ab9:002b` (file `0xbc4b`). This handler does **not** read a bitmap source.
It loops over VGA planes, selects the plane through sequencer map-mask port
`0x3c4`, calls `1ab9:05cf`, and fills the destination with the returned word:

```asm
1ab9:002b  bb 01 01           mov bx,0101
1ab9:002e  ba c4 03           mov dx,03c4
1ab9:0031  8a e7              mov ah,bh
1ab9:0033  b0 02              mov al,02
1ab9:0035  ef                 out dx,ax          ; map mask = 1,2,4,8
1ab9:0036  e8 96 05           call 05cf
1ab9:0039  e8 0b 00           call 0047          ; repeated STOSW rows
...
1ab9:0047  ... f3 ab          rep stosw
```

`1ab9:05cf` (file `0xc1ef`) builds that fill word from command bytes
`+0x22..+0x25` by testing the current plane bit (`BH = 1,2,4,8`) against each
byte, forming a 4-pixel nibble, duplicating it into both nibbles, and copying it
to `AH`:

```asm
1ab9:05cf  33 c0              xor ax,ax
1ab9:05d1  8a 54 22           mov dl,[si+22]
1ab9:05d4  84 d7              test bh,dl
...
1ab9:05f5  8a d0              mov dl,al
1ab9:05f7  d0 e2              shl dl,1           ; four shifts total
1ab9:05ff  0a c2              or al,dl
1ab9:0601  8a e0              mov ah,al
```

`FUN_1000_0b88` zeros `+0x22..+0x25` before calling `FUN_1000_7b4a`, so the
actual per-cell base command fills all four planes with zero. There is no source
bitmap and no read from PAV, DEC, BUM, or any `FUN_1000_808e` extra allocation
on this path. The old "base tile" name is therefore misleading for the static
floor clear: it is a fixed color/pattern fill, not an indexed floor tile.

### Pixel format and field mapping (Structural)

The active mode writes planar VGA/EGA-style memory:

- Plane selection is by `out 0x3c4, ax` with Sequencer index `2` and map masks
  `1`, `2`, `4`, `8`.
- The destination is a planar byte address. One byte represents 8 horizontal
  pixels in the selected plane; the mode-0 handler writes words (`stosw`) after
  converting width units to bytes.
- There is no transparency test in the `+0x1c == 0` handler. It overwrites the
  destination planes with the computed pattern word.

The geometry helper `1ab9:0427` (file `0xc047`) is called with `CX = 2` for the
base path, then uses `CX = 8` for Y/height conversion. The byte-derived mapping
is:

| Command field | Mapping used by planar path |
|---|---|
| `+0x14` dest X | shifted left once, so 1 unit = 2 planar bytes = 16 px. |
| `+0x16` dest Y | shifted left three times, so 1 unit = 8 scanlines. |
| `+0x1e` dest width | shifted left once, so 1 unit = 2 planar bytes = 16 px. |
| `+0x20` dest height | shifted left three times, so 1 unit = 8 scanlines. |
| `+0x0e` page/slot | because `103b:5420 = 1`, destination base is loaded from `103b:5415 + 4*(+0x0e)`. |
| `+0x06/+0x08/+0x0a/+0x0c` source fields | computed by the helper, but ignored for base `1ab9:0179` because `103b:541f = 2` makes the common pointer mapper skip source setup. |
| `+0x22..+0x25` | the only mode-0 "source" data: four command bytes used as a repeated 4-pixel planar pattern; base caller sets all four to zero. |

This confirms the existing unit sizes: a playfield cell drawn by
`FUN_1000_0b88` has `+0x1e = 1`, `+0x20 = 2`, therefore `16x16` pixels. On the
bottom row `+0x20 = 1`, therefore only `16x8` pixels are filled.

### `1cec` helper targets requested in the task (Structural)

The raw targets named by Ghidra are present, but they are not on the direct
`FUN_1000_7b4a -> 1ab9:0179` base path.

Using `file = 0x1090 + (raw_linear - 0x20000)`:

- raw `0x2fcad` = runtime/Ghidra `1cec:2ded`, file `0x10d3d`.
- raw `0x2fc2d` = runtime/Ghidra `1cec:2d6d`, file `0x10cbd`.

At `1cec:31b7` (file `0x11107`) the bytes are:

```asm
1cec:31b7  b8 3b 10           mov ax,103b
1cec:31c1  8b 46 04           mov ax,[bp+4]
1cec:31c4  a3 f0 67           mov [67f0],ax
1cec:31ca  89 16 f2 67        mov [67f2],dx
1cec:31d2  26 8a 45 0a        mov al,es:[di+0a]
1cec:31e3  0e                 push cs
1cec:31e4  e8 ff fb           call 2ded
...
1cec:31fb  0e                 push cs
1cec:31fc  e8 68 fb           call 2d6d
```

`1cec:2ded` preprocesses a list/record into internal records rooted at
`103b:56ee/56f0`. `1cec:2d6d` then dispatches through a `CS:` table at
`1cec:2d9c` using `103b:541d`. This is another video-driver selector dispatch,
not the `+0x1c` command-mode table used by the direct base draw.

No `push 0x00c5` immediate appears in the static bytes around `1cec:31b7`; the
byte at raw `0x300c5` / file `0x11155` is `5d` (`pop bp`). The decompiler's
`func_0x0002fc2d(..., 0xc5)` rendering should therefore not be treated as a
literal instruction without further Ghidra p-code investigation.

### Slot registration and asset buffers (Structural)

`FUN_1000_736f` at `1000:736f` (file `0x83ff`) takes a slot in `DI`, multiplies
it by `0x0a`, and indexes the descriptor table pointed to by `103b:a1b4`.
`FUN_1000_745e` at `1000:745e` (file `0x84ee`) is a thin wrapper around the
common loader at `1000:7235`, passing descriptor base `0xa3ae`.

Those routines explain PAV/DEC/BUM registration, but the base-mode draw above
does not consult any registered source slot. Slot 0 is used by object drawing
because object commands explicitly set source far pointer/coordinates; it is not
the hidden source of the `+0x1c == 0` base fill.

### Caveats / next static step

- **Structural:** The base per-cell command is fixed, not indexed; the caller
  zeros the only pattern bytes. If a C++ port needs the original visual floor,
  render mode 0 as a planar color-index-0 fill unless later runtime evidence
  proves the video memory already contains a nonzero backdrop before these
  per-cell clears.
- **Hypothesis:** Other overlay entry points may use command mode 0 with
  nonzero `+0x22..+0x25` as a repeated 4-pixel pattern fill. That behavior is
  structurally supported by `1ab9:05cf`, but not observed in this static task.
- **Structural:** No original asset or decoded leveldata file was modified
  during this recovery.

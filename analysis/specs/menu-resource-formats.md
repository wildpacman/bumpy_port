# Confirmed menu resource formats

All offsets below are zero-based file offsets. Multi-byte values in both
confirmed menu formats are unsigned and big-endian. `1000:745e` is the common
resource load/decode function reached after `1000:736f` opens the descriptor.

## Layered VEC

`TITRE.VEC` and `MASKBUMP.VEC` use the same 12-byte layer header:

| Offset | Width | Meaning |
|---|---:|---|
| `0x00` | 4 | Decoded layer size |
| `0x04` | 4 | Auxiliary value retained by the original decoder |
| `0x08` | 2 | Method in bits `0..5`; bit `15` marks the final layer |
| `0x0a` | 2 | XOR of the preceding five big-endian 16-bit words |
| `0x0c` | rest | Method payload |

The original validation path rejects a decoded size whose high word exceeds
`0x000f`, a method greater than 30, reserved method-flag bits, a negative
dispatch-table entry, and a header XOR mismatch. A positive method flag causes
the decoder to process another embedded layer; a flag with bit 15 set ends the
chain.

Method 4 is marker RLE. The first payload byte is the marker. A non-marker byte
is literal. `marker, marker` emits one marker. `marker, value, count` emits
`value` `count` times, with count zero meaning 256. The decoder requires the
result size declared by the header.

Method 12 is marker-mask expansion. Its payload starts with a big-endian
16-bit marker whose low byte is emitted by marker positions. It is followed by
`ceil(output_size / 32)` big-endian 32-bit masks and then a separate literal
stream. Masks are processed most-significant bit first. A one bit emits the
marker byte; a zero bit consumes and emits the next byte from the literal
stream. Decoding stops exactly at the declared output size and requires the
literal stream to be consumed exactly.

`MASKBUMP.VEC` has three layers: outer method 4 produces 3358 bytes, method 12
produces 6560 bytes, and final method 12 produces 32099 bytes. The exact final
SHA-256 is
`a737e851ab831c3233d4ef9c3f39fb429bb846a1d33bb6b9e738a0b1c3e1d65a`.

Worked headers:

```text
TITRE.VEC:
00007d63 49dc3356 8004 87ed

MASKBUMP.VEC outer:
00000d1e 6539f5f1 0004 e591

MASKBUMP.VEC embedded after method 4:
000019a0 fbd58e23 000c 6c5a
```

## Decoded screen image format (320x200 planar VGA)

Every supplied `.VEC` decodes to exactly 32099 bytes. For `TITRE.VEC` this buffer
is a full-screen 320x200 VGA image, laid out as:

| Region | Offset | Size | Meaning |
|---|---:|---:|---|
| header | `0x00` | 51 | screen metadata (mostly zero in `TITRE.VEC`) |
| palette | `0x33` (51) | 48 | 16 RGB triplets, 6-bit VGA DAC values (0..63) |
| pixel data | `0x63` (99) | 32000 | four 8000-byte bit-planes |

The pixel data is four **plane-sequential** bit-planes of 8000 bytes each
(`320*200/8`). Pixel *n* (row-major, `n = y*320 + x`) takes its byte at
`n >> 3` within each plane and its bit at `7 - (n & 7)` (**most-significant bit
first**); the four extracted bits form the 4-bit colour index `bit0 | bit1<<1 |
bit2<<2 | bit3<<3`. Each palette component expands to 8-bit via
`(v << 2) | (v >> 4)`.

Confirmed for `TITRE.VEC` by deplaning and comparing to the original title by eye:
golden "BUMPY'S" logo, red "ARCADE FANTASY", green menu items over a blue
gradient. Implemented in `src/video/menu_renderer.cpp` (`deplane_screen`,
`apply_screen_palette`). The other 32099-byte screens (`MONDE1..9.VEC`,
`BUMPRESE.VEC`, `DESSFIN.VEC`) are assumed to share this header layout but are not
yet each visually checked.

Note: an earlier note placed the palette at offset `0x23`; the recovered offset
is `0x33` (51), directly preceding the pixel data. No EXE DAC trace was needed —
the palette travels inside the decoded screen.

## BUMSPJEU.BIN offset-delimited sprite archive

`BUMSPJEU.BIN` is the sprite archive loaded immediately before the game menu.
It starts with three big-endian 32-bit offsets:

```text
0000000c 00000090 00000114
```

They point to three group tables at offsets 12, 144, and 276. Each group table
contains exactly 33 unsigned big-endian 32-bit child offsets. Concatenating the
three tables produces 99 unique, globally increasing offsets. The first child
starts at 408, immediately after the index, and every child starts inside the
89116-byte file.

A child block starts at its indexed offset and ends at the next child offset.
The final child block ends at EOF. Thus the index and the 99 child blocks
partition every byte of the file:

| Region | Start | End | Size |
|---|---:|---:|---:|
| root table | 0 | 12 | 12 |
| group table 0 | 12 | 144 | 132 |
| group table 1 | 144 | 276 | 132 |
| group table 2 | 276 | 408 | 132 |
| child blocks 0 through 98 | 408 | 89116 | 88708 |

There is no stored child count, child length, command byte, or terminator.
Counts are fixed by the root/group layout; lengths come from the following
offset; EOF terminates the last child. The supplied archive has no padding or
trailing bytes. The original common loader relocates the big-endian offsets,
then `1000:2ef8` selects archive index 0 and passes it to `1000:93d8`.

The consumer reached from `1000:93d8` resolves the selected child pointer and
reads six 16-bit graphics fields from the 12 bytes immediately preceding a
resolved pixel pointer. It copies those fields, clamps the first field to 3,
tests flag bits `0x40` and `0x20` in the second field, and uses the fifth field
shifted right by two times the sixth field as a pixel-data size. Those are
consumer-side sprite semantics; they do not alter the archive's exact
offset-delimited byte layout.

The deterministic probe rejects a truncated root/group table, a root layout
other than `12, 144, 276`, a child outside the file, duplicate or decreasing
child offsets, an index/payload gap, and any byte left outside the final
EOF-delimited child block.

Worked child boundaries:

```text
group 0 entry 0: 00000198 .. 0000021c  (132 bytes)
group 0 entry 1: 0000021c .. 000002a0  (132 bytes)
group 2 entry 32: 00004678 .. 00015c1c (71172 bytes, ends at EOF)
```

### Second level: frame-pointer tables and the shared pixel blob (Confirmed structure)

The 99 children are NOT 99 sprites. Each child *except the last* is a
variable-length table of **big-endian 32-bit offsets** (child sizes vary:
52–524 bytes, i.e. 13–131 entries). The **last child** (group 2 entry 32,
`0x4678..EOF`, ~71076 bytes) is a shared byte blob. Earlier structural probes
identified child 0 entries such as `0x46e4, 0x4760, 0x47ec, …`; those are table
offsets consumed by the original setup mutator, not the final menu marker frame
records rendered by `1cec:31b7`. The setup code rewrites big-endian offsets into
far pointers and also mutates pointed records in-place before the blitter reads
them.

### Menu cursor sprite format (confirmed)

Static disassembly note: the requested absolute targets `0x2fcad` and
`0x2fc2d` are loaded-memory addresses, not physical offsets in the 112336-byte
`analysis/generated/BUMPY.UNPACKED.EXE`. They resolve to `1cec:2ded` and
`1cec:2d6d` after Ghidra's `0x10000` load base. `1cec:31b7` calls them in order.

The menu cursor is not BUMSPJEU offset `0x800`. `FUN_1000_0a07` loads level-table
resource index 9 into the `0x898`-byte buffer `DAT_203b_6c2c:6c2e`; the resource
table at `203b:0090` names index 9 as `FLECHE.BIN` and gives size `0x0898`.
`FUN_1000_35a5` sets command block `203b:792e` fields 3 and 4 to that buffer,
uses frame index 0, and draws it at `(0x30, 0x70 + row*0x10)`.

For VGA mode (`DAT_203b_541d == 1`), `FUN_1000_93d8 -> 1cec:2ced` dispatches to
`1cec:0c34`. That helper reads a big-endian 32-bit frame pointer, normalizes it
to the loaded buffer with an `+0x800` offset, calls `1cec:0c77` to byte-swap the
six header words and perform native in-place setup, and advances to the next
pointer until a zero pointer is reached. `1cec:2ded` later resolves
`command.frame_table[command.frame_index]`, copies the six header fields from the
12 bytes immediately before the resolved pixel pointer, and expands only frames
whose flag byte has `0x40` set. The cursor frame has no compressed flag.

Frame table entries are therefore big-endian 32-bit offsets to pixel data:

```text
resolved_data_offset = 0x800 + be32(frame_table[frame_index])
header_offset        = resolved_data_offset - 12
```

Frame headers are six big-endian 16-bit words:

| Word | Meaning |
|---:|---|
| 0 | Auxiliary mask/prefix count copied by `1cec:2ded` and clamped to at most 3. Cursor frame 0 uses 0. |
| 1 | Format flags. Bits `0x40` and `0x20` select the compressed expansion paths in `1cec:2ded`; cursor frame 0 uses `0x0003`, so pixels are raw planar data. |
| 2 | **Y** origin/hotspot used by the blitter anchor math (top = y − word2). Cursor frame 0 uses 0. The pair is stored Y-first: every 32px overlay frame has word3 = 15 (horizontal centre) while word2 tracks the content height — e.g. the ball-on-cloud composite `0x21` (32×21) is (7, 15); reading the pair as (x, y) mis-anchored every asymmetric frame (the world-2 flying-cloud height bug, 2026-07-03). |
| 3 | **X** origin/hotspot used by the blitter anchor math (left = x − word3). Cursor frame 0 uses 0. |
| 4 | Width units. Pixel width is `word4 * 4`; the VGA setup processes `word4 >> 2` 16-pixel groups per row. Cursor frame 0 uses 4, i.e. 16 pixels. |
| 5 | Height in pixels. Cursor frame 0 uses 16. |

Uncompressed archive pixel data follows the 12-byte header. It is four-plane VGA
data laid out in **16-pixel groups**: a row of `width` pixels is `width/16` groups
(`= width_units >> 2`), and each group is 4 planes × 2 bytes = **8 bytes**, ordered
`P0 P1 P2 P3` for that group's 16 pixels. So a row is
`[group0: P0 P1 P2 P3][group1: P0 P1 P2 P3]…`, **not** plane-major across the whole
row. Bits are consumed most-significant first; the four plane bits form the colour
index `p0 | p1<<1 | p2<<2 | p3<<3`. Colour index 0 is transparent for sprite
composition; the port maps it to `0xff` before drawing.

`width` is always a multiple of 16 (`width_units % 4 == 0`). For a 16px frame (one
group) the group layout is identical to plain plane-major, which is why narrow
frames (the cursor, the Bumpy faces, the 16px collectibles) always decoded
correctly — while 32px frames (bumpers, the Bumpy-on-cloud avatar) came out
scrambled until the group layout was applied. This per-group ordering is the
reshuffle `1cec:0c77` performs on uncompressed pixels (line "leaves the pixel
stream untouched … for compressed frames" implies it *does* reshuffle uncompressed
ones), and it matches the compressed format's group description below.

Pseudocode:

```text
pointer = be32(archive[frame_index * 4 : frame_index * 4 + 4])
assert pointer != 0
data = 0x800 + pointer
header = be16[6] at data - 12
assert (header[1] & 0xc0) == 0

width = header[4] * 4          # always a multiple of 16
height = header[5]
groups_per_row = width / 16    # = width_units >> 2
row_stride = groups_per_row * 8
for y in 0..height-1:
    for x in 0..width-1:
        group = x / 16
        xg = x % 16
        colour = 0
        for plane in 0..3:
            b = archive[data + y*row_stride + group*8 + plane*2 + xg/8]
            colour |= ((b >> (7 - (x & 7))) & 1) << plane
        output[y][x] = 0xff if colour == 0 else colour
```

Worked cursor frame-0 example from `FLECHE.BIN`:

```text
0x0000 frame table:
0000000c 00000000

resolved_data_offset = 0x800 + 0x0000000c = 0x080c
header at 0x0800:
0000 0003 0000 0000 0004 0010

width = 0x0004 * 4 = 16 pixels
height = 0x0010 = 16 pixels
pixel bytes = 0x0004 * 0x0010 * 2 = 128

first visible row bytes at 0x0824:
00 00 08 00 08 00 08 00
```

### Compressed sprite frames (Recovered from disassembly; unused by supplied assets)

Frames whose flags word has bit `0x40` set are RLE-of-zero-planes compressed and
expanded by `1cec:2ded` into a scratch buffer before the normal blit. The
algorithm was recovered from the ground-truth machine code (saved in
`analysis/generated/static_blitter_bytes.json`, disassembled with capstone in
16-bit real mode), not from the noisy Ghidra pseudocode. Per-frame relocation is
`1cec:0c34`: each big-endian-32 frame-table entry `L` becomes a far pointer whose
linear address is `archive_base + L + 0x800` — i.e. **pixel data is at file offset
`entry + 0x800`** (the same `+0x800` convention as the uncompressed cursor).
`1cec:0c77` byte-swaps the 12-byte header to native LE and, **for compressed
frames, leaves the pixel stream untouched** (it only reshuffles uncompressed
pixels), so the on-disk compressed stream is exactly what `2ded` consumes.

Stream layout (after the 12-byte header): the frame is `width = width_units*4`
px × `height` rows; a "group" is 16 px across all 4 planes = 8 plane-bytes.
`nGroups = (width_units>>2) * height` **control bytes** come first, then a
**data stream**. Each control byte's 8 bits (MSB first) map to the 8 plane-bytes
of one group: a set bit means "read the next data byte" and a clear bit means
"this plane-byte is 0" (transparent). Read data bytes pass through a 256-entry
lookup table built by `1cec:3137`; for VGA (`DAT_203b_541d != 0`) that table is
`table[i] = bitreverse8(i)`. Flag bit `0x20` (given `0x40`) selects a second
variant (`@2ee9`) that uses the reverse table and a different per-group byte
order; without `0x20` (`@3003`) the bytes are emitted directly. `cs:[0xded]`
(= the VGA flag) gates an additional plane byte-swap.

**No supplied asset uses this path.** `BUMSPJEU.BIN` (the gameplay sprite bank)
is 404+ frames all with flags `0x0003`; `FLECHE.BIN` is uncompressed; the files
the loader names for compressed sprites (`BUMPYSPR.BIN`, `SPRITE.BIN`) are not in
the asset set. The decoder is therefore recovered and documented but deliberately
not transcribed into the port (nothing to decode, nothing to validate against).
The level entity sprites all resolve to **uncompressed** `BUMSPJEU` frames — see
`analysis/specs/level-formats.md` ("Entity sprite bank").

### Uncompressed cursor frame (worked example)

For that row, plane 1, 2, and 3 have bit 4 set while plane 0 is clear, so pixel
`x=4` decodes to colour `0x0e`. The full non-transparent mask is:

```text
................
................
................
....#...........
....##..........
....###.........
....####........
....#####.......
....######......
....#####.......
....####........
....###.........
....##..........
....#...........
................
................
```

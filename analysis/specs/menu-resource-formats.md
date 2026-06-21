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
variable-length table of **big-endian 32-bit frame pointers** (child sizes vary:
52–524 bytes, i.e. 13–131 entries). The **last child** (group 2 entry 32,
`0x4678..EOF`, ~71076 bytes) is a single shared **pixel-data blob**; every frame
pointer in every table points into it. (child 0 = 33 frame pointers:
`0x46e4, 0x4760, 0x47ec, …`, frame sizes 124/140/124/156…). The loader relocates
these big-endian offsets into far pointers at load time.

### Frame pixel format (NOT yet decoded — blocker)

A frame's bytes (e.g. `0x46e4`, 124 bytes) do **not** parse as a clean
planar/chunky bitmap, and the 6×u16 consumer header (field0 clamped to 3, field1
flags `0x40`/`0x20`, field4 `>>` 2·field5 = pixel-data size; from `1000:93d8` →
`1cec:2ced`) is read at a **runtime-relocated** address, so the static file bytes
don't line up with it. The actual blit (`1000:942a` → `1cec:31b7` →
`func_0x0002fcad`/`func_0x0002fc2d`) is **hand-written assembly** the decompiler
left unresolved.

Cheap static guessing of the format did not converge. The efficient unblock is
**dynamic capture** via the DOSBox-X harness: dump the relocated archive and the
rendered VGA framebuffer of the menu, then match bytes to ground-truth pixels.
The menu selection marker is frame index 0 of this archive (see
`menu-behavior.md`).

# Confirmed game-menu behavior

The native port enters the VGA game-menu path directly. The DOS EGA/VGA and
sound-card selectors are setup screens and are not part of the native menu.

`1000:2ef8` prepares the menu graphics. It loads resource indices 0 and 1,
mapped to `BUMSPJEU.BIN` and `MASKBUMP.VEC`, then invokes the sprite-archive
setup and image drawing paths. `1000:35a5` loads resource index `0x12`, mapped
to `TITRE.VEC`, draws it, copies the confirmed 16-byte VGA palette fragment
from data offset `0x064a` when VGA mode flag `203b:541d` equals 1, and enters
the four-row selection loop.

Fully recovered from `FUN_1000_35a5` (the VGA game-menu loop):

- The screen is `TITRE.VEC` (resource `0x12`), blitted full-frame from pixel
  offset 99 (`FUN_1000_80bc` with source = image `+99`). The menu text
  (`PLAY`/`HIGH-SCORE`/`LEVEL`/`PASSWORD`) is **baked into** `TITRE.VEC`, not
  drawn per-glyph here.
- In VGA mode (`203b:541d == 1`) the loader patches **16 bytes from data
  `0x64a`** into the decoded image at offset `0x23` (the 16 bytes preceding the
  48-byte RGB palette at `0x33`; likely the attribute-controller index map).
- The **selection marker is a sprite**: command block at data `0x792e`
  (`DAT_203b_8884`), frame index 0, drawn via `1000:942a` at x `0x30` (48),
  y `0x70 + cursor_row * 0x10` (112 + row·16). During the marker draw,
  `FUN_1000_35a5` points the command at the preprocessed `FLECHE.BIN` workspace
  (`DAT_203b_6c2c:6c2e`), not raw `BUMSPJEU.BIN`; it restores the BUMSPJEU base
  afterwards. `cursor_row` is `local_7`.
- Each loop iteration also blits a level-dependent `(6,2)` patch (source from the
  3-entry table at data `0x75e`/`0x760`, indexed by the LEVEL value
  `DAT_203b_79b5`) to dest `(11,18)` — the `LEVEL: EASY/…` indicator, NOT the
  cursor. (An earlier note mislabelled this `(11,18)/(6,2)` as the cursor.)

Input bits read through `1000:1dde` are:

| Bit | Action |
|---:|---|
| `0x01` | Move up when row is greater than 0 |
| `0x02` | Move down when row is less than 3 |
| `0x10` | Confirm |

Rows do not wrap. Confirm on rows 0, 1, or 3 returns that row. Confirm on row 2
cycles `203b:79b5` through values 0, 1, and 2 and remains in the menu. After
each iteration the input byte is cleared and `1000:75a2` is polled until the
keys are released. On exit, the selected cycle value indexes a three-byte
table copied from data offset `0x11b2` and updates `203b:854f`.

The title draw path passes decoded size `0x7d63` to `1000:7b5a`. Exact native
palette conversion, framebuffer composition, transparency, and pixel hashes
are verified by later rendering and comparison tasks using the original VGA
reference captures.

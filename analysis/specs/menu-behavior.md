# Confirmed game-menu behavior

The native port enters the VGA game-menu path directly. The DOS EGA/VGA and
sound-card selectors are setup screens and are not part of the native menu.

`1000:2ef8` prepares the menu graphics. It loads resource indices 0 and 1,
mapped to `BUMSPJEU.BIN` and `MASKBUMP.VEC`, then invokes the sprite-archive
setup and image drawing paths. `1000:35a5` loads resource index `0x12`, mapped
to `TITRE.VEC`, draws it, copies the confirmed 16-byte VGA palette fragment
from data offset `0x064a` when VGA mode flag `203b:541d` equals 1, and enters
the four-row selection loop.

The cursor starts on row 0. Its draw request uses source rectangle
`(11,18)` with size `(6,2)`. The row marker drawn through `1000:942a` is placed
at x `0x30` and y `0x70 + row * 0x10`.

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

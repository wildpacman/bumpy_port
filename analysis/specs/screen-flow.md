# Screen flow & world map (Confirmed from disassembly)

How the original sequences its screens — recovered from the loader segment
(`1000:*`) in `analysis/generated/decomp/all_functions.c`. Addresses are
`segment:offset`; data-segment offsets resolve in `BUMPY.UNPACKED.EXE` at file
offset `0x11440 + DS_offset` (data segment `0x103b`).

## Main game loop — `FUN_1000_0c18`

```
33c5  (init)
2ef8  (load BUMSPJEU + the menu screen)
LAB_0c2c:                              <- menu restart point
  while ((sel = FUN_1000_35a5()) != 0)  // menu loop (draws the FLECHE cursor)
      sel == 1 -> FUN_1000_5681         // (sub-screen)
      else     -> FUN_1000_0f7a         // (sub-screen: password/options)
  // sel == 0 => "start game":
  do {
      FUN_1000_2d14()                   // load D{world}.PAV/DEC/BUM; DAT_854e = 1
      while (true) {
          FUN_1000_3852()               // *** WORLD MAP *** (navigate nodes, pick one)
              // escape on the map sets DAT_928d = -1 -> FUN_11eb; goto LAB_0c2c (menu)
          DAT_7310 = DAT_854e - 1        // board index = selected node - 1
          ... board setup + the in-level gameplay loop (draw layers, physics, input) ...
          // on win: FUN_3e8a; DAT_79b2++ (next world); == 10 -> FUN_3ed4 (game end)
      }
      FUN_0d9d()
  }
```

The screen order is therefore **menu → world map → playfield → (back to map)**.
`DAT_203b_79b2` is the world number (1..9); `DAT_203b_854e` is the current map
node (1-based). The port's current `App` jumps menu → playfield directly and
pages boards with `←/→`; that is a stand-in for **node selection on the world
map**, which is the missing screen.

## World map screen — `FUN_1000_3852`

1. Loads the per-world full screen (resource index `world + 7`, decoded to the
   32099-byte screen `0x7d63`) into buffer `DAT_7926:DAT_7928`. This is
   **`MONDE{world}.VEC`** — confirmed for world 1 by eye against
   `screenshots/bumpy_001.png`. The node rings are **baked into the MONDE art**.
2. VGA palette patch (`if DAT_541d == 1`): copies **16 bytes** into the decoded
   screen at offset `0x23` from the per-world table `0x6e6[world]` (see Palette
   below). The 16-RGB DAC palette travels in the screen itself at `0x33`.
3. `FUN_1000_3467` draws the playfield frame/border.
4. `FUN_1000_0816(score, …, 7, …)` draws the **7-digit score** (this routine is a
   decimal number formatter, not the avatar).
5. Avatar position `(DAT_9290, DAT_9292)` is initialised from `(DAT_791c,
   DAT_791e)`, set in `FUN_1000_2d14` to `(0x1f, 0x1f)` for most worlds and
   `(0x6f, 0x1f)` for worlds 2 and 5.
6. Node-draw pass `FUN_1000_3c4f`: iterates nodes `1..` and for each with a
   non-zero record byte draws a marker (sprite **frame `0x1da`**) at its position;
   the visible rings, however, are part of the MONDE backdrop.
7. Navigation loop (`while not selected`): reads input bits `DAT_8244`:
   `1`=up→`3ab2`, `2`=down→`3b0f`, `4`=left→`3b6c`, `8`=right→`3bc9`,
   `0x10`=fire→`3cf7`, else Escape (`7ab4` → `DAT_928d = -1`, back to menu).

### Node graph — per world at `0x10c8[world]` (far pointer)

An array of **9-byte records** indexed by node number (1-based), terminated by a
record whose first byte is `0xff`. `FUN_1000_2d14` zeroes byte 0 of every node at
level init (all nodes start "open").

| Byte | Meaning |
|---:|---|
| 0 | node state (0 = open/enterable; `0xff` = end-of-table terminator) |
| 1 | **up** neighbour node # (0 = no link) |
| 2 | up distance in px (`>>2` = number of 4px animation steps) |
| 3 / 4 | **down** neighbour / distance |
| 5 / 6 | **left** neighbour / distance |
| 7 / 8 | **right** neighbour / distance |

Moving (`FUN_1000_3ab2`/`3b0f`/`3b6c`/`3bc9`): if the neighbour byte is non-zero,
set `DAT_854e` = neighbour and animate the avatar `distance>>2` steps of ±4px in
that axis (`DAT_9292` for up/down, `DAT_9290` for left/right), redrawing each step
via `FUN_1000_3c26`.

### Node positions — per world at `0x10ec[world]` (far pointer)

`(x, y)` word pairs indexed by `node - 1`; the marker sprite and the avatar land
on these. Horizontal node spacing is 80px, vertical 48px (matching the graph
distances; a `96` distance skips a row).

### World 1 (extracted: graph `DS:0x09e6`, positions `DS:0x0a80`)

15 nodes on a 4×4 grid (minus the `(112,80)` slot):

```
 node : pos        : links
   1  : ( 32, 32)  : R2
   2  : (112, 32)  : L1  D9
   3  : (192, 32)  : R4
   4  : (272, 32)  : L3  D7
   5  : ( 32, 80)  : D8
   6  : (192, 80)  : D10 R7
   7  : (272, 80)  : U4  L6
   8  : ( 32,128)  : U5  D12 R9
   9  : (112,128)  : U2  L8
  10  : (192,128)  : U6  R11
  11  : (272,128)  : L10 D15
  12  : ( 32,176)  : U8  R13
  13  : (112,176)  : L12 R14
  14  : (192,176)  : L13 R15
  15  : (272,176)  : U11 L14
```

The avatar starts on node 1 (`DAT_854e = 1`).

### Selecting a node — `FUN_1000_3cf7`

Fire on the current node: animate the avatar into the ring, compute a spawn cell
(`FUN_1000_3dfd`: `DAT_9d36`/`DAT_9d38` from the avatar pixel position, clamped to
0..18 / 0..22), set `DAT_854f = 0xaa`, and return "selected" so the map loop
exits. Back in `FUN_1000_0c18`, **`DAT_7310 = DAT_854e - 1`** becomes the board
index: `FUN_1000_32b0` reads the BUM board record at `DAT_6bf2 + (node-1)*0xc2`
(`0xc2` = 194). So **map node N → playfield board N-1**; world 1's 15 nodes map to
D1's 15 boards.

## Palette mechanism

Every full screen (TITRE / MONDE / menu) carries its own **16-colour VGA DAC
palette at decoded offset `0x33`** (48 bytes, 16 RGB6 triplets) — this is what the
port's `apply_palette` / `apply_screen_palette` reads, and it produces correct
menu/title/map colours. In addition the game overwrites **16 bytes at screen
offset `0x23`** (the EGA/attribute-controller palette registers) from a
context-specific table, in VGA mode:

| Screen | Patch source |
|---|---|
| Menu (`35a5`) | `0x64a` |
| World map (`3852`) | per-world `0x6e6[world]` |
| `2fac` (menu/score backdrop) | `0x63a` |
| `08d1` / `0d9d` / `0f7a` / `11eb` | `0x70e` / `0x71e` |

The **playfield loads no backdrop of its own** (its base is a flat colour-0 clear;
see level-formats.md), so it **inherits the world map's per-world MONDE palette**.
That is why world 1's board renders brown — it is the world-1 palette, and is
faithful. The port currently applies only the DAC palette (`0x33`); the 16-byte
`0x23` patch (an EGA-register remap) is not applied, but `MONDE1.VEC` matches the
real capture by eye without it, so it appears identity/inert for the VGA DAC path.
A DOSBox in-level capture would confirm the playfield palette exactly.

## What this means for the port

- The "this isn't a game cycle" gap is the **missing world-map screen**. To match
  the original, add a map screen between the menu and the playfield: render
  `MONDE{world}.VEC`, draw the Bumpy avatar at the current node, move it between
  linked nodes with the arrows (graph above), and on fire enter board `node-1`.
  The `←/→` board paging then retires (node selection replaces it).
- The brown palette is **not a bug**: the playfield faithfully inherits the
  per-world MONDE palette.

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
          ... board setup + the in-level gameplay loop ...   // see game-loop.md
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
3. `FUN_1000_3467` plays the **edge-to-centre darken** over the *previous* visible
   screen (see "Screen-change darken" below) — not a frame/border draw, as an
   earlier note here assumed.
4. `FUN_1000_0816(score, …, 7, …)` draws the **7-digit score** (this routine is a
   decimal number formatter, not the avatar).
5. Avatar position `(DAT_9290, DAT_9292)` is initialised from `(DAT_791c,
   DAT_791e)`, set in `FUN_1000_2d14` to `(0x1f, 0x1f)` for most worlds and
   `(0x6f, 0x1f)` for worlds 2 and 5.
6. Node-draw pass `FUN_1000_3c4f`: iterates nodes `1..` and for each with a
   non-zero record byte draws a marker (sprite **frame `0x1da`**) at its position;
   the visible rings, however, are part of the MONDE backdrop.
7. Navigation loop (`while not selected`): each iteration re-reads the **currently
   held** keys via `FUN_1000_1dde` → `FUN_1000_75a2` (which ORs the live key-state
   table at `DAT_4d42`) into `DAT_8244`, then dispatches **one** action:
   `1`=up→`3ab2`, `2`=down→`3b0f`, `4`=left→`3b6c`, `8`=right→`3bc9`,
   `0x10`=fire→`3cf7`, else Escape (`7ab4` → `DAT_928d = -1`, back to menu).
   There is **no release debounce**: a move (`3ab2`..`3bc9`) animates the whole
   `dist>>2`-step slide inline (each step waits a retrace via `3c26`→`7bdd`), and the
   loop then re-polls — so **holding a direction walks node to node continuously**, the
   slide being the only pacing. The port reproduces this in `WorldMap::update`
   (directions act every tick; only fire/cancel keep a release guard).

## HUD — score + lives (Confirmed from disassembly)

The score/lives HUD is redrawn on both the world map and the in-level playfield.

- **Score** — `FUN_1000_0816(DAT_a0d4, DAT_a0d6, 7, X, Y)` formats the 32-bit score
  (`a0d4` low / `a0d6` high) as **7 zero-padded decimal digits** and blits it through
  the text system (`07f0` → `9837` cursor + `9804` draw). `X, Y` are **raw pixels** and
  `Y` is the glyph **baseline** (top scanline = `Y − ascent`). On the world map (call
  ~line 4283) it is `(1, 8)` → first digit top-left `(1, 1)`. The glyphs come from the
  **DDFNT2.CAR** bitmap font, drawn in palette index **14** — NOT the BUMSPJEU sprite
  bank. Full format in **"## HUD score font"** below. The in-level call (`…,7,0,7`) is
  gated (only when an event flag is set), so normal play shows no persistent in-level
  score — consistent with `screenshots/bumpy_002.png`.
- **Lives** — `FUN_1000_6130` draws `DAT_791a` copies of sprite **frame `0x1aa`**
  in a row: for life index `i = lives..1`, sprite x = `i*8 + 0x50`, y = `0`
  (`*DAT_a0d0 = (uint)i*8 + 0x50; FUN_1000_942a(0x7986, …)`). Called immediately
  after the score on each screen (line 4284 on the map). Verified by eye against the
  red Bumpy-head row in `screenshots/bumpy_001.png`.

## Game-loop close — win / lose / world-complete (Confirmed from disassembly)

The **world map is the hub**: after the in-level loop `FUN_1000_0c18` exits (on
`DAT_856d` dead / `DAT_9d30` won / `DAT_928d` quit), control returns to the map
unless the game is over or the world is complete. State carried across boards:
score (`a0d4/a0d6`), lives (`791a`), and per-node completion.

- **Win** (`FUN_1000_1e3d`, ball fell into the exit portal): sets `DAT_9d30 = 1`
  **and** marks the node — `*_DAT_9baa = 1`, i.e. byte 0 of the current node's
  9-byte graph record (`0x10c8[world]` table, see "Node graph" below). The node
  then shows the completed marker (`0x1da`, `FUN_1000_3c4f`).
- **All nodes cleared** (`FUN_1000_3e8a`): ANDs byte 0 of every node record; if
  all are 1 the world is complete → `DAT_79b2++` (world number). At world `10`
  (`'\n'`) the game plays the outro `FUN_1000_3ed4`; otherwise the next world's
  MONDE/D-files load via `FUN_1000_2d14`, which also **clears all node bytes**.
- **Lose a life** (`FUN_1000_22fc`): always sets `DAT_856d = 1`; if `DAT_791a == 0`
  it sets `DAT_928d = 0xff` (**game over** → main loop returns to the menu), else
  `DAT_791a--`. The node is **not** marked, so a failed board is replayable from
  the map. `'#'` collectible → `DAT_791a++` (extra life). Lives init `5` (`2d14`).

## Screen-change darken — `FUN_1000_3467` (Confirmed from disassembly)

Called at the start of the menu (`35a5`), world map (`3852`), score backdrop (`2fac`)
and password (`11eb`) screens, and before a board loads (`0c18` right after the map
returns a selected node) — i.e. on **every** screen change. It darkens the *outgoing*
visible screen from the **edges inward to the centre**, then the new screen is presented.

Mechanism: it paints **concentric black rings** over the visible page, outermost first,
in the engine's **20×25 character cells** (each **16×8 px**). Per ring it fills four
black bars — top, bottom, left, right — via the rectangle-fill primitive
`FUN_1000_7b4a` (descriptor colour bytes `+0x22..0x25 = 0` → index 0), each committed by
`FUN_1000_9864` (`FUN_2036_0000`, a per-mode latch-flush — **not** a retrace wait). The
loop runs **10 rings**, shrinking the bar span by two cells per ring (`local_4`: 20→2,
`local_5`: 25→7); together the rings cover the whole screen. After `s` rings the still-
visible centre is the rectangle `[16s, 320−16s) × [8s, 200−8s)` cells-worth of pixels;
at `s = 10` it is empty, so the screen is fully black.

There is **no retrace wait inside the loop** (only the `9864` latch-flush), so on the
original the close is one fast CPU-bound burst. The port (`src/video/screen_transition`)
reproduces the geometry exactly and the run loop holds each ring
`kDarkenFramesPerRing` retraces (default **2**, i.e. ~35 Hz → the 10-ring close lasts
~0.29 s) so the wipe is visibly paced; tune `kDarkenFramesPerRing` (in
`platform_sdl3/sdl_app`) to match a given DOSBox cycle setting more closely.

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

### Selecting a node — `FUN_1000_3cf7` (the fire-to-enter cloud-jump)

Fire on an open node (`*DAT_9bac:DAT_9baa == 0`) plays a short jump animation, then
computes a spawn cell (`FUN_1000_3dfd`: `DAT_9d36`/`DAT_9d38` from the avatar pixel
position, clamped to 0..18 / 0..22), sets `DAT_854f = 0xaa`, and returns "selected"
so the map loop exits. Back in `FUN_1000_0c18`, **`DAT_7310 = DAT_854e - 1`** becomes
the board index: `FUN_1000_32b0` reads the BUM board record at `DAT_6bf2 +
(node-1)*0xc2` (`0xc2` = 194). So **map node N → playfield board N-1**; world 1's 15
nodes map to D1's 15 boards.

**The jump animation** is a scripted sprite sequence driven by `FUN_1000_13df`, one
step per tick (same cadence as the slide's `FUN_1000_3c26`):

- `3cf7` sets the animation script pointer `DAT_a1ac = 0x203b:0x1114` and the step
  counter `DAT_824d = 0x16` (22 records), plays a launch sound (`FUN_1000_6e11`),
  saves the avatar position to `DAT_791c/791e`, and **pre-draws the launch cloud**
  (`DAT_824a = 0xcb`) at the avatar descriptor offset `(-0xf, +3)`.
- The script at **DS:0x1114** (file `0x12554` = `0x11440 + 0x1114`) is 22 records of
  three `int16` each — `{frame, dx, dy}`. Each tick `13df` sets `DAT_824a = frame`,
  moves the avatar by `(dx, dy)` (dx negated if `DAT_9bae != 0`), advances the pointer
  6 bytes, and decrements `DAT_824d`. **Every dx is 0**, so the jump is purely
  vertical. Frames + cumulative dy:

  | phase | frames | cumulative dy |
  |---|---|---|
  | squash in place | 1,2,3,4,5,6,7,0 | 0 throughout |
  | bounce up | 1,2,3,4,5,6,7,0 | -3,-5,-7,-8,-8,-7,-6,-5 |
  | arc down (stretched) | 0x13,0x16,0x19,0x1c,0x1f | -4,-1,+2,+5,+8 |
  | vanish | 100 (blitter skips it) | +8 |

  `3cf7` also issues two leading draws of frame 0 before the loop, so the displayed
  sequence is 24 ticks. `FUN_1000_1cb2` blits `DAT_824a` at `(DAT_9290, DAT_9292)`
  and skips frame `100`.

**Sprite layout (recovered by pixel-exact alignment, not the frame `origin` words).**
The resting avatar **frame `0x21`** (32×21) is a composite: Bumpy (the ball) on top
of a cloud. Its sub-frames are reused standalone by the jump:

- **frame `0`** (16×15, the neutral ball) is **pixel-identical** to the top of `0x21`
  at content offset **(8, 0)** — i.e. horizontally centred in the 32-wide box;
- **frame `0xcb`** (32×11, the launch cloud) matches the bottom of `0x21` at content
  offset **(0, 10)** — bottom-aligned in the box.

So the port places every avatar frame **centred horizontally on the node** inside the
resting `0x21` box (ball top-aligned + bounced by the cumulative dy, cloud
bottom-aligned). This reconstructs the resting pose exactly at jump start and keeps the
cloud stationary while Bumpy bounces. (The frame-header `origin_x/origin_y` words are
**not** the composite anchor: origin-aligning the parts lurches them ~(8,8) off, which
the original visibly does not do.) The blit routine itself is an unresolved far-call
(`FUN_1cec_31b7 → func_0x0002fc2d`), so this placement is recovered from the sprite
data + the verified resting capture, not from the blitter.

## Frame timing — vertical-retrace paced (Confirmed from disassembly)

The game advances its logic **once per displayed frame** and paces each frame on the
**vertical retrace**, not on a timer. The wait is `FUN_1000_7bdd(1)` →
`FUN_1ab9_0351`, which clears a frame flag and tail-dispatches per video mode:
`mov bp,[DAT_541d]; shl bp,1; jmp word ptr [bp+0x5475]`. The dispatch target is the
only vertical-retrace poll in the whole image (unpacked-EXE file offset `0x11405`):

```asm
BA DA 03   mov dx, 0x03DA      ; VGA/CGA Input Status #1
EC         in  al, dx
A8 08      test al, 8          ; bit 3 = vertical retrace
74 FB      jz  $-5             ; spin until retrace begins
EC         in  al, dx
A8 08      test al, 8
75 FB      jnz $-5             ; spin until retrace ends
C3         ret
```

(An adjacent twin polls `0x3BA` bit 7 for the mono/Hercules path; `DAT_541d` selects
which.) So **the slide, the cloud-jump, and gameplay all step at the display's vertical
refresh**. For the pinned VGA 320×200 16-colour mode that is **70.086 Hz** (EGA: 60 Hz),
so e.g. the 24-frame cloud-jump lasts `24 / 70.086 ≈ 0.342 s`.

Beware a red herring: the game **reprograms the PIT (`FUN_1000_7f9a`, `out 0x43,0x36`)
to ~19.2 kHz** from a reload table at `DS:0x54de` — that is the **PC-speaker sample
timer**, not the frame clock. Frame pacing is purely the retrace poll above.

The port reproduces this with a fixed **70.086 Hz** game tick in
`src/platform_sdl3/sdl_app` (one logic update per frame, sleep/spin to the next
boundary), decoupled from the host monitor — replacing the earlier ~60 Hz
`SDL_Delay(16)` loop.

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

The **playfield loads no backdrop screen of its own** (its base is a flat colour-0
clear; see level-formats.md), but it does **not** inherit the world map's MONDE
palette. It has its **own per-board palette** baked into the DEC board header:
`FUN_1000_0604` → `FUN_1000_063b` reads 16 big-endian words from the board record
and `FUN_1000_08d1`'s VGA branch builds a DAC palette from them (R = high byte,
G = low nibble-pair, B = low nibble, each `<< 3`). So world 1's board is **dark
blue**, matching `screenshots/bumpy_002.png` — not the brown MONDE map palette of
`screenshots/bumpy_001.png`. Full recovery + the word format are in
level-formats.md ("D?.DEC board palette"); implemented in `LevelBoard::palette()` /
`render_board`.

(An earlier note here wrongly concluded the brown was "faithful" because the board
inherits MONDE — that was an unverified hypothesis, corrected once the board was
rendered with the recovered per-board palette and compared to the original.)

## What this means for the port

- The "this isn't a game cycle" gap is the **missing world-map screen**. To match
  the original, add a map screen between the menu and the playfield: render
  `MONDE{world}.VEC`, draw the Bumpy avatar at the current node, move it between
  linked nodes with the arrows (graph above), and on fire enter board `node-1`.
  The `←/→` board paging then retires (node selection replaces it).
- The brown palette is **not a bug**: the playfield faithfully inherits the
  per-world MONDE palette.

## HUD score font (Confirmed from disassembly + asset)

The 7-digit HUD score uses the bitmap font **`DDFNT2.CAR`** (1981 bytes, on
disk in the game root; `.CAR` = *caractères*). Recovered 2026-06-26.

### Provenance / load path

- `FUN_1000_808e(size_lo,size_hi,...)` is **`malloc`**, NOT a resource
  resolver: it tail-calls `FUN_1cd5_0000` = DOS *Allocate Memory*
  (`AH=48h, INT 21h`, rounds DX:AX up to paragraphs) and returns the new
  block as far ptr `segment:0`. So the earlier "`FUN_1000_808e(0x7c3)` =
  resource id 0x7c3" reading was wrong — **`0x7c3` is the byte-size** of the
  font buffer (and `0x898` is the byte-size of the `FLECHE.BIN` arrow buffer).
- The buffers are allocated in `FUN_1000_0416` (font→`DAT_75da:DAT_75dc`,
  arrow→`DAT_6c2c:DAT_6c2e`) and filled in `FUN_1000_0a07`:
  `FUN_1000_736f(4,4)` opens **LEVEL-table index 4** and
  `FUN_1000_745e(h,DAT_75da,DAT_75dc,DAT_00be,DAT_00c0,0)` reads it raw
  (`DAT_00be:DAT_00c0 == 0x7c3`).
- LEVEL resource table @ `203b:0090` (10-byte entries; bytes [6:8] of each
  entry = file size): **index 4 = `DDFNT2  .CAR` size 0x07c3**, index 9 =
  `FLECHE  .BIN` size 0x0898. (The MENU table @ `203b:0928` has
  `SCORE.VEC` at index 3, but that is a different/unused-for-HUD asset; the
  active HUD font is DDFNT2.CAR set by `FUN_1000_0a07`→`FUN_1000_97d5`.)
- `FUN_1000_97d5`→`FUN_1ab9_132b` makes it the active font: far ptr
  `DAT_68a4:DAT_68a2`, and `DAT_693e = desc[3]+2`.

### Draw path

`FUN_1000_0816(score_lo,score_hi,ndigits,X,Y)` formats the 32-bit score
(`DAT_a0d4`/`DAT_a0d6`) into an `ndigits`-wide space-padded decimal string,
then `FUN_1000_07f0(str,strseg,X,Y)`:
- `FUN_1000_9837(X,Y)`→`FUN_1ab9_1441`: `DAT_6942=X`, `DAT_6944=Y` — **raw
  pixel coordinates**, passed UNSCALED (contrast `FUN_1000_07ad`, which scales
  char cells: `X=col<<3`, `Y=row*8+7`).
- `FUN_1000_9804(str)`→`FUN_1ab9_13ec`: per char, `FUN_1ab9_13bc` checks
  `desc[0] <= ch < desc[1]` and dispatches the VGA rasterizer
  `*(DS:0x6952 + DAT_541d*2)` = `FUN_1ab9_1607` (video-mode index `DAT_541d==1`).

### Font descriptor format (`DDFNT2.CAR`)

Glyph lookup (`FUN_1ab9_14d3`): `idx = ch - desc[0];`
`rec = desc_base + BE16(desc + 6 + idx*2)` (the offset-table entries are
**big-endian** u16, relative to the descriptor base).

```
header:
  [0] u8  first_char           = 0x20 (' ')
  [1] u8  last_char_exclusive  = 0xff
  [2] u8  baseline ascent      = 7     ; glyph top scanline = cursorY - desc[2]
  [3] u8  line metric          = 8     ; DAT_693e = desc[3]+2
  [4] u8  inter-char spacing   = 1     ; x_advance = glyph_width + desc[4]
  [5] u8  reserved             = 0
  [6 .. 6+2*(last-first)-1]  BE16 per-char glyph-record offsets (rel. to base)

per-glyph record:
  [0] u8  width  (pixels)
  [1] u8  height (rows)
  [2] u8  y-offset (rows below glyph top; 0 for digits)
  [3..]   bitmap, row-major, ceil(width/8) bytes per row, MSB-first
```

The rasterizer reads `height` rows of `ceil(width/8)` bytes from the record,
MSB-first; foreground pixels are written into a 320-wide 16-colour **planar**
buffer (`offset = Y*40 + X/8`, bit `X&7`). After each glyph it advances
`DAT_6942 += width + desc[4]` (proportional spacing). The font is **variable
width**.

### Digit glyphs (verified)

Extracted by `tools/re/dump_hud_font.py`; ASCII-art dump saved to
`analysis/generated/hud_font_glyphs.txt`.

All digits are 7 rows tall. Widths: 0,2,3,4,5,7,8 = 7px; 1 = 4px; 6,9 = 8px.
Space = width 4, height 0 (blank), advance 5.

```
char  w h  bytes(hex)           x_advance
' '   4 0  (none)               5
'0'   7 7  7cc6c6c6c6c67c       8
'1'   4 7  60e060606060f0       5
'2'   7 7  7cc6067cc0c0fe       8
'3'   7 7  7cc6067c06c67c       8
'4'   7 7  c0c0ccccfe0c0c       8
'5'   7 7  fec0c0fc06c67c       8
'6'   8 7  3e63607e63633e       9
'7'   7 7  fec60c18303030       8
'8'   7 7  7cc6c67cc6c67c       8
'9'   8 7  3e63633f03633e       9
```

(Each row byte: MSB = leftmost pixel; e.g. `0x7c = .#####.` for width 7.)

### Score position (pixels)

`X,Y` from `FUN_1000_0816` are pixel coordinates; **`Y` is the baseline**, and
glyph top scanline = `Y - desc[2]` = `Y - 7`.

- **World map** (`FUN_1000_3852`, decomp ~line 4283):
  `FUN_1000_0816(DAT_a0d4,DAT_a0d6,7,1,8)` → cursor (1,8) → first digit
  top-left at pixel **(1, 1)**, glyphs 7px tall (y = 1..7). Seven `'0'`
  digits at 8px advance span x = 1..56 — matches `screenshots/bumpy_001.png`
  (score at top-left ≈ px (0..56, 0..8)).
- **Per-digit advance** = `glyph_width + 1`; for `'0'` that is **8px**
  (the all-zero default score `0000000` is evenly spaced).

### In-level HUD

`FUN_1000_0816` *is* reachable from the in-level loop `FUN_1000_0c18`, but
only **conditionally**: `FUN_1000_0c18` calls `FUN_1000_49d7` (decomp ~line
1243) **only when** `FUN_1000_7ab4(0x19)` is non-zero — that helper just reads
the event/key flag array `DAT_4d42[0x19]`. `FUN_1000_49d7` then draws the
score via `FUN_1000_0816(DAT_a0d4,DAT_a0d6,7,0,7)` → cursor (0,7) → top-left
pixel **(0, 0)**. Because it is gated on that flag (not drawn every frame),
normal play shows **no persistent in-level HUD** — consistent with
`screenshots/bumpy_002.png`. The only always-on score HUD is the world map.

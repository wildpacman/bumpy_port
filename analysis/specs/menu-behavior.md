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

## Difficulty selection — the `LEVEL` menu item (Confirmed)

Row 2 (`LEVEL`) is a **difficulty / game-speed selector**, not an exit. Recovered
from `FUN_1000_35a5` (menu loop), `FUN_1000_51d8`/`FUN_1000_2ef8` (label prep),
`FUN_1000_1349`/`FUN_1000_05e7` (the in-level speed effect), and the data tables
at `DS:0x11b2` / `DS:0x75e`.

**Selection state.** `DAT_203b_79b5` ∈ {0,1,2}. It is reset to **0 (EASY)** on
every fresh menu entry (`LAB_1000_0c2c`, line 1164). Confirm on row 2 cycles it
`0→1→2→0` and stays in the menu; confirm on rows 0/1/3 exits with that row.

**On-screen indicator (each menu frame).** The current label
`EASY`/`MEDIUM`/`HARD` is blitted over the `LEVEL: ___` row. The three label
bitmaps live in **`MASKBUMP.VEC`** (menu resource index 1), not `TITRE.VEC`:
`FUN_1000_2ef8` decodes `MASKBUMP.VEC` into the shared screen workspace, then
`FUN_1000_51d8` grabs three `6×2`-char-cell (**96×16 px**) regions at char
`(0, 13)`, `(0, 17)`, `(0, 21)` = pixel `(0, 104/136/168)` into scratch buffers
(`DS:0x8b88`, `DS:0x824e`, `DS:0x8582`). The menu loop re-blits the selected one
(far-ptr table `DS:0x75e`, indexed by `79b5`) **opaquely** to dest char
`(11, 18)` = pixel **(176, 144)** — the LEVEL row. `TITRE.VEC` bakes
"`LEVEL: EASY`" as the default so idx 0 matches the backdrop.

**Speed effect (the actual difficulty).** On menu exit,
`DAT_203b_854f = table[79b5]` where the 3-byte table at **`DS:0x11b2` = `{0xff,
0xaa, 0x00}`**. In the in-level frame loop (`FUN_1000_0c18`, line 1235)
`FUN_1000_1349` runs once per frame: it reads the low bit of `854f`, waits **2**
vertical retraces (`FUN_1000_05e7(2)` → `FUN_1000_9864` ×2) if the bit is set
else **1**, then rotates `854f` right (low bit → bit 7). More set bits ⇒ more
waits ⇒ slower ⇒ easier:

| idx | label | `854f` | bits | retraces/frame | rate |
|---:|---|---|---:|---|---|
| 0 | EASY (default) | `0xff` | 8 | always 2 | 35.043 Hz |
| 1 | MEDIUM | `0xaa` | 4 | alternate 2/1 | ~46.7 Hz |
| 2 | HARD | `0x00` | 0 | always 1 | 70.086 Hz |

Note: the port's historical in-level pace of **35.043 Hz** (two retraces/step,
pinned empirically) is exactly EASY (`0xff`) — i.e. the "second retrace the
disassembly didn't show" **is** `FUN_1000_1349`→`FUN_1000_9864`.

**Scope.** `854f` only paces the in-level loop. The world map saves it into
`DAT_203b_8e8a`, forces `854f = 0` for the map (`FUN_1000_3852`), and restores
it on return; the fire-to-enter cloud-jump forces `854f = 0xaa`
(`FUN_1000_3cf7`). In-level debug keys **F1–F5** override `854f` live to
`0/0x88/0xaa/0xee/0xff` (`FUN_1000_1d26`).

## Password screen — the `PASSWORD` menu item (Confirmed)

Row 3 (`PASSWORD`) opens the code-entry screen `FUN_1000_0f7a`; a valid code
jumps the run to that world. Recovered from `FUN_1000_0f7a` (entry screen),
`FUN_1000_5c87` (the 6-char editor + validation), `FUN_1000_2d14` (world start),
and the tables `DS:0x135c` / message pointers `DS:0x11a2..0x11aa`. Confirmed
against the raw disassembly (Ghidra dropped the `736f`/`a9f5` args).

**Background = BLACK (not the HALL OF FAME art).** Both password screens
`7307(0x928)` + `736f(3)` + `745e(…,99,0)` load menu resource **index 3 =
SCORE.VEC** into the offscreen work buffer and install **its palette only** (16
bytes from `DS:0x71e` patched at `0x23` in VGA mode; index 0 = black). They do
**NOT** paint the image: the two steps the real high-score screen `FUN_1000_5681`
uses to make SCORE.VEC visible — `FUN_1000_7b5a` (deplane, `FUN_1c28_0000`) and
`FUN_1000_80bc` (full-frame blit of image `+99`) — are **absent** from `0f7a`,
`0d9d`, and `11eb`. The `FUN_1000_3467` screen-darken leaves the page black, and
the password text is drawn over black. This is identical to GAME OVER
(`FUN_1000_11eb`), which was corrected the same way — an earlier note here wrongly
claimed the framed backdrop shows; it does not.

**Glyphs.** Every character is drawn as a BUMSPJEU sprite `char + 0x175` (`'A'`
`0x41` → frame `0x1b6`); `0x20` (space) is skipped. Same font the HIGH-SCORE
screen uses.

**Layout** (`FUN_1000_0f7a`, `[0]=x=col*16`, `[1]=y`):
- prompt `ENTER YOUR PASSWORD` (`DS:0x11a2→0x12f5`, 19 ch) at `y=16`, cols 0–18;
- 6-char entry field, seeded `AAAAAA`, at `y=160` (0xa0), cols 7–12;
- result message at `y=96` (0x60), cols 3–16: ` PASSWORD OK  ` (`DS:0x11a6→0x1309`,
  14 ch) on a match, else `PASSWORD ERROR` (`DS:0x11aa→0x1318`, 14 ch).

**Editor** (`FUN_1000_5c87`, twin of the high-score name entry `59d3`): fill the
6 cells with `'A'`, then UP/DOWN step a **sprite-frame index** over the
**contiguous** glyph run `0x1ac..0x1d0` = `'0'-'9','A'-'Z','.'` (verified by
dumping the bank; the glyph sheet is compact — `'9'`=`0x1b5` is adjacent to
`'A'`=`0x1b6`, and `'.'`=`0x1d0`). The cycle is **CLAMPED, not wrapped**:
- **UP** = frame − 1 (guard `frame >= 0x1ad`), so from the `'A'` seed it walks
  `9,8,…,0` and **floors at `'0'`** (`0x1ac`);
- **DOWN** = frame + 1 (guard `frame <= 0x1cf`; `'Z'`→`0x1d0`='.'), so it walks
  `B,…,Z,.` and **ceils at `'.'`** (`0x1d0`).

The buffer stores `frame − 0x175` (correct ASCII for `'A'`-`'Z'`; the digit and
`'.'` frames store non-letter bytes, which simply never match a password).
LEFT/RIGHT move the caret across the 6 columns, **fire (`0x10`) commits**. The
caret cell **blinks its glyph on/off** — the blink toggles `local_8 & 8` (**8
frames glyph / 8 frames a black `FUN_1000_7b4a` rectangle** that erases it), i.e.
the letter flashes, there is **no coloured cursor block** (an earlier port drew a
solid block; and note `0x1d0` is the `'.'` glyph, so using it as a "caret" put a
stray dot in the cell — both wrong). Held-repeat cadence is a steady **8
frames/step**; a fresh press steps once immediately. **Blink period and
held-cadence confirmed against the original capture `screenshots/bumpy_000.avi`.**
The port adds a ~18-frame initial delay before the first auto-repeat (so a tap =
one step) — a deliberate playability nicety over the original's constant rate.

(Earlier this section mis-stated the run as `[0x1ad,0x1cf]`=`'1'-'9','A'-'Z'` with
a `0x1d0→0x1a3` wrap and inverted UP/DOWN — corrected after dumping the frames.)

**Validation + start world.** On commit, compare the 6 chars against the 8
passwords at `DS:0x135c` (`table[i] = *(word*)(0x135c + i*4)` → a 6-byte string);
a match sets the world to `i + 2`. The 8 codes (worlds 2–9):

| World | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|
| Code | `ACCESS` | `BUTTON` | `ISLAND` | `PRETTY` | `WINNER` | `ZOMBIE` | `LOVELY` | `SYSTEM` |

`FUN_1000_0f7a` sets `DAT_79b2` = the matched world (2–9), or `1` on no match
(showing `PASSWORD ERROR`). It then returns to the menu; the next `PLAY` starts
at world `DAT_79b2` (`FUN_1000_2d14` loads that world's graph/positions and
patches the `?.PAV/DEC/BUM` digit). `LAB_0c2c` does **not** reset `79b2`, so the
chosen world persists until overwritten. `2d14` calls the cosmetic "starting at
world N" intro `FUN_1000_4015` (sets `DAT_119a`) — decorative, not ported.

**Sibling: password *display* `FUN_1000_0d9d`.** Shown between worlds (main loop,
after `3e8a` world-complete + `79b2++`): same **black** page (identical background
sequence to `0f7a`/`11eb` — palette-only, no `7b5a`/`80bc`), prompt `YOUR
PASSWORD` (`DS:0x119e→0x12e7`, 13 ch) at `y=80`, then the current world's code at
`y=112` from base `DS:0x1354` (`*(word*)(0x1354 + world*4)`, world-indexed = the
same table), waits for fire. This is a separate between-worlds feature, not the
menu item (not currently ported).

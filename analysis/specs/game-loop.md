# In-level game loop (Stage 3)

Recovered 2026-06-22 by reading `analysis/generated/decomp/all_functions.c`
(Ghidra) and spot-disassembling `BUMPY.UNPACKED.EXE` with capstone. Evidence
levels per the project ladder (Structural / Hypothesis / Confirmed). Addresses
are `segment:offset`; data-segment offsets (`DAT_203b_*`, seg `0x103b`) resolve
in `BUMPY.UNPACKED.EXE` at file offset `0x11440 + off`.

This documents the playfield loop that the screen-flow spec left as
`... board setup + the in-level gameplay loop ...`. See
[screen-flow.md](screen-flow.md) for the outer menu→map→playfield sequencer and
[level-formats.md](level-formats.md) for the `D?.PAV/.DEC/.BUM` containers.

## The loop nest — `FUN_1000_0c18` (Confirmed)

The whole game is one function. Pseudocode of the playfield portion (decomp
lines ~1175-1257):

```
do {
  FUN_1000_2d14()                         // per-WORLD init: load D{w}.PAV/.DEC/.BUM
  while (true) {
    if (DAT_928d != 0) return             // hard quit
    FUN_1000_3852()                       // *** WORLD MAP *** pick a node -> DAT_854e
    if (DAT_928d == -1) { ...; goto menu }
    DAT_7310 = DAT_854e - 1               // board index = node - 1
    <per-board SETUP>                      // lines 1189-1210 (see below)
    while (DAT_928d==0 && DAT_856d==0 && DAT_9d30==0) {   // *** PER-FRAME LOOP ***
      <frame body>                         // lines 1212-1244 (see below)
    }
    if (DAT_928d == -1) { FUN_11eb(); FUN_5681(); continue }   // quit-to-menu
    if (FUN_1000_3e8a()) {                 // all nodes in the world cleared?
      DAT_79b2++                           // -> next world
      if (DAT_79b2 == 10) FUN_1000_3ed4()  // all 9 worlds done -> outro
    }
  }
  FUN_1000_0d9d()                          // (world-transition screen)
} while (true)
```

### Loop-exit flags (Confirmed)

The per-frame loop runs while all three are zero (decomp line 1211):

| Flag | Meaning | Set by |
|---|---|---|
| `DAT_928d` | quit / escape (`1`=quit-to-menu via F7, `0xff`=restart, `-1`=escape) | `1d26` (F7), `22fc`, `3ed4` |
| `DAT_856d` | **level cleared (win)** | `22fc` only |
| `DAT_9d30` | **player dead** | `1e3d` only (terminal death-anim frame) |

`0bf9` (in setup) clears `9d30`/`856d`; `0c18` clears `928d` at the menu entry.

## Per-board setup — lines 1189-1210 (Confirmed)

Runs once when a node is entered, before the frame loop:

| Call | Role |
|---|---|
| `FUN_1000_3467` | draw playfield frame/border |
| `FUN_1000_0bf9` | **instantiate board**: `32b0` (load record) → `2a78` (decode 3 tile planes) → `31de` → clear win/death flags |
| `FUN_1000_4bc6(DAT_8562)` | load the moving-entity movement script for behavior id `8562` |
| `FUN_1000_5181(1,0)` | init the player avatar object (state 1, dir 0; bounds 20×25) |
| `FUN_1000_0604` | **apply the per-board 16-colour DEC palette** (see level-formats / screen-flow) |
| `FUN_1000_328f` | wait for the screen-reveal "curtain" to finish |
| `FUN_1000_05e7(1)` | one page-flip |
| the `138c/13b2 … 19a1/19e4` pairs (twice) | prime both page buffers with the initial background so the first animated frame has something to restore over |

## Per-frame body — lines 1212-1244 (Confirmed order)

```
DAT_79b3 = FUN_1000_93b1()   // advance PRNG (NOT input — see below)
138c; 13b2                   // snapshot last frame's scroll tile-coords -> "prev" slots
13df                         // step PLAYER movement/animation script ({frame,dx,dy})
4c14                         // step the second entity's movement script
1473; 4b4e                   // pixel pos -> tile cell (player; entity)
14e4; 15a1                   // step layer-A (3 obj) / layer-B (4 obj) frame animation
1a20                         // timed transient overlay (spark/flash), countdown a1a8
165e; 17c7                   // build+submit layer-A / layer-B object sprites
5085; 50c0                   // build AABB boxes: player (5085) and entity (50c0)
1a67; 1b2b                   // submit per-object layer-A / layer-B sprites
1bd7; 1c41                   // submit the two scrolling background tile layers (opaque)
1cb2; 1cea                   // rasterize the two foreground sprites: player; entity
FUN_1000_7bdd(1)             // *** VSYNC + page-flip *** (frame barrier)
1349                         // 2-phase cadence -> 05e7(1|2) page-flips
629c                         // step the entity behaviour byte-script (spawns layer-B)
1d26                         // *** PLAYER TICK ***: function keys + state machine
4c99                         // entity vs grid collision + AI move
50fb                         // player-box vs entity-box overlap -> sets hit flag a1aa
233a                         // 10-frame periodic timed hook (auto-repeat/demo)
if (FUN_1000_7ab4(0x19)) FUN_1000_49d7()   // 'P' pressed -> pause overlay
```

Render pipeline is back-to-front: scrolling backgrounds → object layers →
player/entity sprites → present at vsync. Backgrounds submit through
`FUN_1000_93b8` (mode flag `541f=1`), normal sprites through `FUN_1000_80bc`,
and the two foreground sprites bypass the list via the immediate rasterizer
`FUN_1000_942a`. The actual VGA plane/latch work lives in the unrecovered
overlay segment `0x1cec` (`func_0x0002f…`); none of `1000:*` touches
`0x3c4/0x3ce/0xa000` directly. (mode-flag semantics = Hypothesis.)

### Frame pacing (Confirmed)

`FUN_1000_7bdd(1)` is the only frame clock — it dispatches per video mode
(`DAT_541d`) into the driver's vertical-retrace poll. Pinned VGA 320×200 16-colour
= **70.086 Hz**; the port already paces at this rate
(`src/platform_sdl3/sdl_app`). The PIT reprogram (`out 0x43,0x36`, ~19.2 kHz) is
the PC-speaker sample timer, not the frame clock — red herring.

## The player ball state machine (Confirmed — the core of gameplay)

The ball does **not** use continuous velocity/gravity integration. Motion is
**keyframe-scripted**: a state decides an action, which loads a `{frame:u16,
dx:i16, dy:i16}` script; subsequent frames step that script, translating the ball
pixel pos and animating the sprite until the step count hits zero, then the next
decision runs.

### Driver — `FUN_1000_1d26` each frame

1. Poll function keys via `7ab4`: F1-F6 set debug byte `854f` (0/0x88/0xaa/0xee/
   0xff); **F2 → `22fc` (force win)**; **F7 → `928d=1` (quit-to-menu)**.
2. Then advance the ball:
   - `a1aa != 0` (enemy hit pending) → `FUN_1000_228d` → death path (`4263(0x2e)`).
   - else `236f` (sample tile under ball → `7924`), `1dde` (read input → `8244`),
     then: `824d==0` → `1e02` (decide new action) ; `824d!=0` → `238e` (play out
     the current scripted action frame).

`824d` = movement-script steps remaining. **`824d==0` ⇒ decide, `824d!=0` ⇒
animate.**

### State dispatch — `FUN_1000_1e02` → table at `DS:0x7ca`

`FUN_1000_1e02` saves prev state (`8552`), and unless a forced-fall is pending
(`a0ce==0 && a1a7!=0` → `27de`), calls `*(0x7ca + DAT_792c*2)()`. `DAT_792c` is
the player state byte. Recovered table (file offset `0x11c0a`; entries past 0x10
are the idle stub `28f9`):

| `792c` | handler | state |
|---:|---|---|
| 0,1,2,4 | `28f9` | **idle / resting on a tile** (the decision hub) |
| 3, 0xf | `23b6` | **rolling** (move while supported + direction held) |
| 5 | `2423` | **bounce/spring** (scriptless — keeps running script) |
| 0xa | `2470` | **falling-begin** → state 0xb |
| 0xb | `248e` | **falling/float** (scriptless) |
| 0xc | `24d7` | **landing test** |
| 0xd | `250a` | **rolling-after-land** |
| 0xe | `25ad` | **warp** (fall in a hole, pop out the next `0x0f` cell) |
| 0x10 | `22b0` | **level-clear / leave board** → `22fc` |

Death (0x2e), pipe-enter (0x1c), death-anim (0x23/0x24), cleared (0x30) are set
directly via `4263`, routed through the second (animation) table at `DS:0x43c0`
(`238e`: row = state ×0x22, col = `792a` step counter), not via `0x7ca`.

### Animation-step dispatch — `FUN_1000_238e` → table at `DS:0x43c0` (Confirmed)

The decide table above only runs at rest (`824d==0`). **While a scripted move
plays (`824d!=0`), `1d26` calls `238e` instead**, which dispatches a second
function-pointer table at `DS:0x43c0` indexed by `state*0x22 + 792a*2` (so 17
step-slots per state; `792a` is the step counter `13df` advances). The decide
handlers also call `238e` once right after `4263` to run **step 0** of the new
move. This per-step table is where the ball's *cell* advances and where input is
re-read mid-move to chain the next action — without it, motion and control are
incomplete. Resolve it with `tools/re/dump_player_dispatch.py`.

Per-step **micro-op vocabulary** (each slot is a tiny handler; Confirmed unless
noted):

| Handler | Effect |
|---|---|
| `7111` | no-op: render the current frame only (leaf stub, not decompiled) |
| `64e2`/`64ff` | `856e -= 8` / `+= 8` — advance the ball one **row** (UP / DOWN) |
| `651c`/`6535` | `856e -= 1` / `+= 1` — advance the ball one **col** (LEFT / RIGHT) |
| `6611`/`65e5`/`65fb` | input-mask: `8244 &= 0x0f` / `&= 0x10` (keep fire) / `&= 0x1d` (drop RIGHT) |
| `6717` | latch `856f=856e`, run the pickup check `6d26` (collect when over a plane-C cell) |
| `654e` | if fire/left held and not already latched, start a held-bump (`695e`) |
| `6587` | the `0x02`-lane + RIGHT-held special (sets the `79b4=0x34` auto-roll) |
| `4437`/`4344` | the **input-decode tree** (below); re-reads input to chain the next move |
| `6648`/`6699`/`66d8`/`6748`/`6789`/`673a`/`6305` | per-state step-0 entry: set sprite (`6d6a`/`6d94`), play the bump sfx (`6a89`/`6e11`) — sprite/sfx detail = Hypothesis |

So the **cell tracks the pixel move**: e.g. roll-right (state `0x02`) row =
`[entry, nop, nop, nop, 6372, 6535, 6717, 654e, 6587, 65e5, 6627, nop, 65fb]` —
at step 5 `6535` flips `856e` to the next column (mid-way through the 13-step
`+40px` roll), step 6 collects, steps 7-8 re-arm from input. Roll-left (`0x01`)
is the mirror with `651c` at step 5; hop-up (`0x03`) uses `64e2` at step 3;
hop-down (`0x04`) uses `64ff` at step 0.

### Input-decode tree (the `0x43c0` chaining) (Confirmed)

`4437`/`4344` walk a fixed-priority tree over the `8244` input bits, each branch
either arming a move or falling through to the next direction:

```
4437 → fire(0x10)? 440c : 4398
4398 → LEFT(0x01)? 4454 : 43b5         4454: hop UP    (state 0x1d) if cell above clear, else try RIGHT
43b5 → RIGHT(0x02)? 448a : 43d2        448a: hop DOWN  (state 0x1e) if cell below clear, else try UP
43d2 → UP(0x04)?   44c0 : 43ef         44c0: hop up-LEFT  (2634) if left  cell occupied (45cf/4605), else 0x1f
43ef → DOWN(0x08)? 4532 : 440c         4532: hop up-RIGHT (26a1) if right cell occupied, else 0x20
440c → (no dir) settle/pipe (6d94 0x2f / 4305)
```

`45cf(cell)` = plane-A at cell is non-zero and not `0x19`; `4605(cell)` = plane-B
at `cell+0x30` is non-zero and not `0x13` (i.e. "is there structure to bump
against / land on"). This is a diamond control scheme: the four inputs bump the
ball UP / DOWN / up-left / up-right depending on what neighbouring cells hold.

### The key helper — `FUN_1000_4263(newState)`

How a handler both transitions and arms a move in one call:

```
DAT_8244 = 0                       // consume input
if (DAT_8242 == 0) {               // not in a sub-step lock
  DAT_792c = newState              // <-- SET STATE
  if (newState not in {5,0xb,0x1c}) {   // scriptless states keep their script
    DAT_a0dc = 0
    rec = *(0x2252 + newState*4)    // {count, mirror, scriptptr}
    DAT_824d = rec.count           // arm step count
    DAT_9bae = rec.mirror          // facing
    DAT_a1ac = rec.scriptptr       // movement-script cursor
  }
}
```

### Movement step — `FUN_1000_13df` (dual-use, also the cloud-jump stepper)

```
DAT_824a = *a1ac                          // sprite frame
DAT_9290 += (DAT_9bae ? -a1ac[1] : a1ac[1])  // x += dx (mirrored by facing)
DAT_9292 += a1ac[2]                        // y += dy
a1ac += 3 words; DAT_824d--                // advance, decrement count
792a = (824d==0) ? 0 : 792a+1
```

Frame `100` = "hidden" (blitter skips it). Skipped while `824d==0` or state ∈
{5,0xb,0x1c}.

### Input mapping (Confirmed bits, names = Hypothesis)

`FUN_1000_75a2` builds an action bitmask from the keyboard scan tables and the
joystick (port `0x201`); `1dde` stores it (when non-zero) in `DAT_8244`:

| bit | dir | effect (in idle/rolling) |
|---:|---|---|
| 0x01 | LEFT | start left roll/bump |
| 0x02 | RIGHT | start/continue rolling right |
| 0x04 | UP | bump the tile up-left (`2634`); overrides horizontal |
| 0x08 | DOWN | bump the tile down-right (`26a1`); overrides horizontal |
| 0x10 | fire (joy btn 1) | — |
| 0x20 | btn 2 | — |

A direction press never moves the ball directly: the active handler calls
`4263(newState)`, the next frames step the loaded script. `FUN_1000_7ab4(sc)`
reads one scancode from the key-state table `DAT_4d42[sc&0x7f]` (used only for
the function/debug keys and pause).

## Board record — 194 bytes (`0xc2`), decoded by `FUN_1000_2a78` (Confirmed)

`32b0` selects record `node-1` at `DAT_6bf2 + (node-1)*0xc2`, copies its first
0x90 bytes (+6 trailing) into the live grid `DAT_a0d8`. `2a78` then walks a 6-row
× 8-col grid over three planes:

| Offset | Size | Plane | Decode |
|---|---:|---|---|
| `0x00` | 48 | A: pegs/bumpers | `value → 0x3d3a[value] → record 0x3d6a` (sprite + tile coord); col 0-7 |
| `0x30` | 48 | B: blocks | `value → 0x4086[value] → record 0x40a6`; **col 7 skipped** |
| `0x60` | 48 | C: collectibles | sprite `frame = value + 0x179`; pos table `0x274/0x276` |
| `0x90` | 1 | — | start cell `856e` (`-1` if non-zero) |
| `0x91` | 1 | — | start cell 2 `8572` |
| `0x92` | 1 | — | **remaining-collectibles count `a0cf`** |
| `0x93` | 1 | — | entity-2 cell index `8571` (`value-1`; `-1`=none) |
| `0x94` | 1 | — | **entity behaviour id `8562`** (→ `4bc6` script) |
| `0x95` | 1 | — | board sub-type `7920` |
| `0x96` | 1 | — | index into `0x2546` → entity tile/anim base `a0de` |

Palette is **not** in this record — it is the first 16 words of the parallel
812-byte (`0x32c`) DEC block (`0604`/`063b`, big-endian, byte-swapped). Tile
graphics come from the PAV buffer. This matches
[[entity-sprites-from-uncompressed-bumspjeu]] (the same `0x3d3a`/`0x4086`/`0x274`
tables and `value+0x179` collectible mapping).

D-file board counts: BUM `0xb60` (2912) → `(2912-2)/194 = 15` boards (worlds
1,2,3,7,8); the 2330-byte group → 12 boards (worlds 4,5).

## Collect, score, win/lose (Confirmed)

- **Collect** (`6c14`): plays the pickup, clears the collectible cell
  `a0d8[856e+0x60]=0`, decrements `a0cf`. When `a0cf` hits 0 → level-complete
  cascade (`a1b1=1`, sound `0x59`) → leads to `856d=1`.
- **Score** (`6c95`) — 32-bit `DAT_a0d4`(lo)/`a0d6`(hi):
  - base pickup **+250** (`0xfa`)
  - tile `'#'` → **+1 life** (`791a++`) (and +250)
  - tile `'/'` → **+10000**
  - tile `'0'` → **+50000**
  - rendered by the decimal formatter `FUN_1000_0816` (7 digits), not 6c95.
- **Lives** `DAT_791a` (init 5); decremented in `22fc`; when 0 on win → `928d=0xff`.
- **Death** (`50fb`): AABB overlap of player box (`5085`) vs entity box (`50c0`)
  sets `a1aa=1`; next frame `228d` → death state `0x2e`; the terminal anim frame
  `1e3d` sets `9d30=1` and marks the node cleared (`*9baa=1`).
- **Win**: `22fc` sets `856d=1` and runs the finish animation. After the loop,
  `3e8a` ANDs every world-map node's cleared byte; all-clear → `79b2++` (next
  world), `==10` → `3ed4` (outro, then `79b2=1`, `928d=1`).

## What this means for the port

The current `App` treats `Screen::level` as display-only. To make it a game,
implement the per-frame loop as a platform-independent state machine:

1. **Board model** — decode the 194-byte record into the 6×8×3 plane grid + the
   header fields (start cell, collectible count, entity id/cell, sub-type). The
   entity-sprite decode already exists (`src/resources/entity_sprites`).
2. **Player state machine** — the `0x7ca` table states (idle/rolling/falling/
   landing/warp/bounce/clear) driven by `824d` (script steps) + `4263`-style
   transitions + `13df`-style `{frame,dx,dy}` stepping. This is the gameplay;
   port the script tables at `DS:0x2252` (per-state move scripts) and the tile-
   reaction tables (`0x36ee/0x371e/0x36be`, `0x4256/0x4276/…`).
3. **Collision** — tile sampling under the ball (`236f` → `7924`), bump/collect
   on the grid, ball↔entity AABB.
4. **Scoring/lives/win-lose** with the exact constants above.
5. Drive it at the existing 70.086 Hz tick; one logic step per frame.

### Open items / next blockers

- ~~**Move-script tables** at `DS:0x2252`~~ **DONE** — extracted to
  [move-scripts.md](move-scripts.md) (decoder `tools/re/dump_move_scripts.py`,
  calibrated against the cloud-jump script). Confirmed the playfield cell spacing
  is **40px x 32px**: every move script's net displacement is a whole number of
  cells.
- ~~**Tile-value semantics**~~ **DONE** — confirmed against decoded D1 in
  [tile-semantics.md](tile-semantics.md): plane-A behavior is table-driven by
  structure code (5 reaction tables at `DS:0x36be..0x377e` + sprite map
  `0x3d3a`), not fixed constants; the win count `a0cf` excludes free pips/lives.
  (Decoder `tools/re/dump_tile_tables.py`.)
- **Entity AI table** at `DS:0x870` (indexed by `8562`) — the monster movement.
  **DECISION: deferred.** Confirmed from the decoded D1 headers (byte `0x93`):
  of all 15 world-1 boards, only board index 2 (node 3) has an active entity
  (`8571 != 0xff`, id `0x07`); nodes 1 and 2 have none. So the first playable
  board needs no entity AI — build ball + collectibles + win/lose first; add the
  `DS:0x870` AI when implementing node 3.
- The two foreground blits go through the overlay raster core `0x1cec`
  (unrecovered) — the port already has its own blitter, so this only matters for
  pixel-exact comparison, not for a playable port.

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
| `DAT_928d` | quit / escape (`1`=quit-to-menu via F7, `0xff`=out of lives, `-1`=escape) | `1d26` (F7), `22fc` (0 lives), `3ed4` |
| `DAT_856d` | **board ended, lost a life** (enemy death / deadly pit / F2 skip) | `22fc` only |
| `DAT_9d30` | **board CLEARED** (ball fell into the exit portal; node marked done) | `1e3d` only |

`0bf9` (in setup) clears `9d30`/`856d`; `0c18` clears `928d` at the menu entry.

> **Corrected 2026-06-23** (was: `856d`=win, `9d30`=dead). The names were
> transposed. `22fc` *decrements a life* and never marks the node cleared, so it is
> the **lose-a-life** exit (entity death routes `228d`→`0x2e`→`22d2`×3→`22fc`; the
> chute tiles `0x12`/`0x1f` in `253f`→`22b0`→`22fc` are deadly pits; F2 is the skip
> cheat). The real **board clear** is the exit portal: `1e3d` sets `9d30=1` *and*
> marks the world-map node done (`*9baa=1`). See "Exit portal" below.

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

`FUN_1000_7bdd(1)` is the per-frame retrace barrier — it dispatches per video mode
(`DAT_541d`) into the driver's vertical-retrace poll. The VGA 320×200 16-colour
vertical refresh is 70.086 Hz, but the rate is **not uniform across the engine's
loops**: the sequences driven by the `{frame,dx,dy}` script stepper `FUN_1000_13df`
— in-level gameplay and the world-map **cloud-jump** — advance **one step per two
retraces = 35.043 Hz**, while world-map **navigation** (the `FUN_1000_3ab2..3bc9`
slide) and the menu step once per retrace = 70.086 Hz. The retrace handler for mode
1 sits behind a jump table Ghidra could not recover, so the /2 is pinned empirically
(side-by-side with the original under DosBox: at a uniform 70 Hz the ball bounced
twice per original bounce; at a uniform 35 Hz the map node-to-node slide dragged).
The port selects the rate per phase (`src/platform_sdl3/sdl_app`, `half_rate`;
`kVgaRefreshHz` / `kGameTickHz`). The PIT reprogram (`out 0x43,0x36`, ~19.2 kHz) is
the PC-speaker sample timer, not the frame clock — red herring.

## The player ball state machine (Confirmed — the core of gameplay)

The ball does **not** use continuous velocity/gravity integration. Motion is
**keyframe-scripted**: a state decides an action, which loads a `{frame:u16,
dx:i16, dy:i16}` script; subsequent frames step that script, translating the ball
pixel pos and animating the sprite until the step count hits zero, then the next
decision runs.

### Driver — `FUN_1000_1d26` each frame

1. Poll function keys via `7ab4`: F1-F6 set debug byte `854f` (0/0x88/0xaa/0xee/
   0xff); **F2 → `22fc` (skip board — costs a life, see the flags note)**; **F7 →
   `928d=1` (quit-to-menu)**.
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
| 0x10 | `22b0` | **deadly-pit / chute exit** (lose a life) → `22fc` |
| 0x30 | `1e3d` | **exit-portal descent done** → `9d30=1` + node cleared (the real win) |

Pipe-enter (0x1c), death-anim (0x23/0x24) are set directly via `4263`, routed
through the second (animation) table at `DS:0x43c0` (`238e`: row = state ×0x22,
col = `792a` step counter), not via `0x7ca`. State **0x30** is the exit-portal
descent (move script frames 19→32, the ball sinking into the pit, same art as the
0x0e warp fall-in); when its script finishes, the `0x7ca` decide slot for 0x30 is
`1e3d`, which clears the board.

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
| `654e` | if fire/up held and not already latched (`7923==0`), start a held-bump (`695e`): `a1a7` armed → next decide forces a fall (`27de`→`2810`), re-arming the bounce |
| `647e` | bounce-state (`0x06/0x07/0x2b`) step-4/5 variant: bump sfx (cosmetic) **then `654e`** — so a held UP keeps bouncing and recoils the floor every cycle. (Port: route `0x647e`→`f_654e`; omitting it sprang the floor only every other landing.) |
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

### Input mapping (Confirmed — bits *and* directions)

`FUN_1000_75a2` builds an action bitmask from the keyboard scan tables and the
joystick (port `0x201`); `1dde` stores it (when non-zero) in `DAT_8244`. The
direction→bit assignment is read straight from the joystick decoder
`FUN_1000_773c`: the X axis sets `0x04` (low) / `0x08` (high) and the Y axis sets
`0x01` (low) / `0x02` (high). So the bits are vertical-first, **not** the earlier
`left=0x01` guess (which was transposed and produced 90°-rotated controls in the
port):

| bit | dir | effect (in idle/rolling) |
|---:|---|---|
| 0x01 | UP | hop up (`2634` via the `4437` tree → state `0x1d`) |
| 0x02 | DOWN | hop down (state `0x1e`) |
| 0x04 | LEFT | bump the tile up-left (`2634`); starts the left roll |
| 0x08 | RIGHT | bump the tile up-right (`26a1`); starts/continues the right roll |
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

## Tile bump/spring animations — the pegs/blocks reacting (Confirmed)

When the ball bumps a peg or block (and while it rests on one), the tile visibly
springs/recoils. This is a small per-tile animation system, separate from the
ball's own sprite, decoded by `tools/re/dump_object_anim.py` and ported to
`src/game/object_anim.*` + the `LevelGame` slots.

**Slots.** Three layer-A peg slots (`DS:0x4c70`) and four layer-B block slots
(`DS:0x4cbc`). Each slot record: `[0]`=active, `[1]`=grid cell, `[2..5]`=stream
cursor (far ptr), `[6]`=current frame byte, `[8]`=current `y_offset`, `[10]`=current
frame. Stepped every frame by `FUN_1000_14e4` (layer A) / `FUN_1000_15a1` (layer B).

**Stream.** Each step reads one byte: `0xff` frees the slot, `0x00` holds the
previous sprite, otherwise the byte is a **sprite index** resolved through a
**near-pointer record table** — layer A `DS:0x3d6a`, layer B `DS:0x40a6` (segment
tables sit +2 over) — each entry pointing at a `{y_offset, frame_index}` record.
This is the indirection both `FUN_1000_14e4`/`15a1` **and** the setup draw
`FUN_1000_2a78` use; the raw EXE never references `DS:0x37be`. The records are only
*coincidentally* sequential for low indices (so a flat `0x37be + (idx-1)*4` /
`0x3ad2` read agreed for lanes/pegs/blocks), but layer A's `0x3d6a` pointers are
**non-sequential** — e.g. the level-exit pit: sprite idx `0x7e`/`0x7f` resolve to
frames `0xbd`/`0xbe` (the hole + animated down-arrow), **not** the green coils
`0xb5`/`0xb6` a flat read returned. (Layer B's `0x40a6` pointers *are* sequential
into `0x3ad2`, so layer B reads flat with **+0xf1**, `FUN_1000_17c7`.) A frame word
with bit `0x200` is a hidden (blink-off) step.

> **Fixed 2026-06-23.** The port originally read both the static (`entity_sprites`
> `kLayerA`) and the animation (`object_anim` `kAnimRecordA`) frames from the flat
> `0x37be` region, which is right only where the pointers happen to be sequential.
> The **exit portal** diverges: tile `0x20` → sprite `0x7f` → frame `0xbe` (pit +
> down-arrow), but the flat read gave `0xb6` (a green coil/spring), so the open exit
> rendered as a spring. Both tables now resolve layer A through `0x3d6a`
> (`dump_object_anim.py` + the regenerated `kLayerA`). The exit now draws the pit
> statically (`0xbe`) and the pulse animates `0xbd`↔`0xbe` (the blinking arrow). The squash comes from the frame art + its Y anchor: e.g. layer-B block
value 2 rises `f03..f07`, compresses (`f07` at `y17`, held 4 frames), then springs
back up through `y14→12→10→8→6→2`.

**Arming.** `FUN_1000_69aa(id)` (layer A, cell `856f`) / `FUN_1000_6a89(id)` (layer
B, cell `8570`) look up a descriptor (`DS:0x2ede` / `DS:0x3256`) = `{settle tile,
stream ptr}`, write the **settle tile** into the grid plane (so the tile keeps its
post-bump look — for the common `0x01` lane the settle is `0x01`, i.e. no change,
which is why the port ran without it), and arm a free/matching slot.

**Triggers** (event id → `69aa`/`6a89`):

| Where | Trigger |
|---|---|
| rest/idle-blink (`6648`, states `0x00/0x11/0x3c-0x3f`) | `6987(0x3d0a[7924])` — spring the tile under the ball |
| **roll start** (`6699`/`66d8`, when prev state ∉ {3,0xf}) | `6d6a` → `6987(0x3c7a[7924]` / `0x3caa[7924])` — the lane recoiling left/right as it deflects the ball (the "land and slide off" reaction) |
| held bump (`654e`→`695e`) | `69aa(0x3cda[7924])` (same id drives the forced fall) |
| **structure trigger** (`6717`→`6d26`, in states with a `0x6717` step: land `0x04`, roll `0x01/0x02`, bounce `0x1a/0x1b`, …) | `6d94(0x4396[7921])` — spring the structure the ball is on, keyed by its plane-A value. This is the **special-bumper** recoil: `0x14`→`0x2d`, `0x15`→`0x2e` (world-1 node 14's left/right-flinging springs) |
| `0x02`-lane + DOWN (`6587`) | `69aa(0x34)` |
| hop entries (`6748/6789`) | `6d94(0x18/0x19)` (layer-A on ball cell) + layer-B select |
| layer-B neighbour bump (`6699/66d8/67e2/6813/68fe/693a/6890/68bb`) | `6a89(0x35be..0x369e[8551])` keyed by the bumped block's plane-B value |
| fall routing (`2810`) | `69aa(0x76b[79b9*2])` — the 2nd byte of each `DS:0x76a` pair |
| chute top (`253f`), `0x0e` climb (`29a6`) | `69aa(0x24)` |
| hole/warp (`4802`, `25ad`) | `69aa(0x27)` |
| level-clear door (`233a`/`6c14`) | `69aa(0x59/0x5a)` (cosmetic, end of board) |

**Render.** Back-to-front the slot draws over a restored-clean background, so the
port suppresses the static tile under each active slot and overlays the slot's
current `{frame, y_offset}` (`draw_object_anims`). World 1 is all plane-A lanes
(no blocks), so its springs are entirely layer A. Verified by-eye: the lane under
the ball bends into a U and recoils, matching the original.

## Collect, score, win/lose (Confirmed)

- **Collect** (`6c14`): plays the pickup, clears the collectible cell
  `a0d8[856e+0x60]=0`, decrements `a0cf` (free pips `0x01`/lives `0x23` excluded).
  When `a0cf` hits 0 it **opens the exit portal** (see below) — it does NOT clear
  the board.
- **Score** (`6c95`) — 32-bit `DAT_a0d4`(lo)/`a0d6`(hi):
  - base pickup **+250** (`0xfa`)
  - tile `'#'` → **+1 life** (`791a++`) (and +250)
  - tile `'/'` → **+10000**
  - tile `'0'` → **+50000**
  - rendered by the decimal formatter `FUN_1000_0816` (7 digits), not 6c95.
- **Lives** `DAT_791a` (init 5); decremented in `22fc` (the lose-a-life exit); when
  it reaches 0 there → `928d=0xff` (out of lives).
- **Lose a life** (`22fc`, sets `856d=1`): runs the finish animation, `791a--`.
  Reached by entity death (`50fb` AABB of `5085`×`50c0` → `a1aa=1` → `228d` → state
  `0x2e` death tumble → `22d2`×3 → `22fc`), by the deadly chute pits `0x12`/`0x1f`
  (`253f`→`22b0`→`22fc`), and by the F2 debug skip. The node is **not** marked
  cleared, so `3e8a` keeps the node open and the player retries from the map.
- **Win / board clear** (the exit portal): the ball rolls into the opened pit and
  falls in — tile `0x20` → reaction `0x30` → state `0x30` descent → its `0x7ca`
  decide slot `1e3d` → `9d30=1`, `a1a9=1`, `*9baa=1` (node done). After the loop,
  `3e8a` ANDs every node's cleared byte; all-clear → `79b2++` (next world), `==10`
  → `3ed4` (outro, then `79b2=1`, `928d=1`).

### Exit portal (Confirmed) — the level-exit the player must reach

Taking the **last required** collectible does not end the board; it opens a portal
(a pit) that the ball must then reach and fall into:

1. `6c14` (`a0cf==0`): `856f=8572` (header byte `0x91`, the portal cell), then
   `69aa(0x59)` — a layer-A bump event whose **settle tile is `0x20`**, so plane A
   at the portal cell becomes `0x20` and a one-shot "pit opening" animation plays.
   Then `a1b1=1`, `8550=0xf2`.
2. `233a` (every frame, after `1d26`): while `a1b1`, count `8550`; each time it hits
   `9`, reset to 0 and re-arm `69aa(0x5a)` (settle `0x20`, a 6-frame "pit bob") at
   the portal cell — the open portal keeps pulsing.
3. The ball navigates to the portal cell and rests on tile `0x20`. The decide path
   (`28f9`→`2965`→`29a6`→`465e`→`46bb`) reads reaction code `0x30` for tile `0x20`
   (tables `none`/`up`/`down` at `DS:0x36be`+`0x20`) and arms **state `0x30`**.
4. State `0x30` plays the 20-frame descent (the ball sinking into the pit), then its
   decide slot `1e3d` fires → `9d30=1` and the node is marked cleared. Board done.

Ported to `LevelGame`: `f_6c14` now calls `f_69aa(0x59)`; `f_233a` runs the pulse;
the decide dispatch routes state `0x30` to `f_1e3d` (sets `d_9d30`); and `tick()`
maps `9d30`→`won`, `856d`→`dead`. For world-1 board 0 the portal cell is `0x2c`
(col 3, row 5).

## Moving entity — the per-board monster (Confirmed)

Each board may carry **one** moving entity (a monster). Of all 15 world-1 boards
only **board index 2 (node 3)** has one: cell `0x28-1 = 39` (col 7, row 4),
behaviour id `7`, anim-base index `0x10`. Boards with header byte `0x93 == 0` have
no entity (`8571 = 0xff`).

### Spawn / init (`2a78` → `48a9` → `4bc6`, Confirmed)

| Field | From | Meaning |
|---|---|---|
| `8571` | `record[0x93] - 1` | entity cell `0..47` (`0xff` = none) |
| `8562` | `record[0x94]` | **current movement-script id** (also the AI-dispatch index) |
| `7920` | `record[0x95]` | board sub-type — gates the AI's random turn (`79b3 < 7920`) |
| `a0de` | `0x2546[record[0x96]]` | sprite-frame base; board 2's `0x10` → `0x1f7` |
| `8565`/`8564` | `8571>>3` / `8571 & 7` | row / col |
| `79ba`/`79bc` | `0x274[8571] + 7` | entity pixel pos = `bum_cell_position + (7,7)` |

`48a9` runs only when `8571 != 0xff`. `4bc6(8562)` then loads the movement script.

### Movement scripts — `DS:0x2520` far-pointer table (Confirmed)

`4bc6(id)`: `desc = far *0x2520[id]` → `{[0]=count a1b0, [1]=dir 9d2f, [2..3]=kf
ptr a0ba, [4..5]=extra}`. The keyframes are `{frame, dx, dy}` word triples. The 10
scripts (`tools/re/dump_entity_ai.py`):

| id | count | dir | net | role |
|---:|---:|---:|---|---|
| 1 | 8 | 0 | dy −32 | move **UP** one cell (8×dy−4) |
| 2 | 8 | 0 | dy +32 | move **DOWN** one cell |
| 3 | 10 | 1 | dx −40 | move **LEFT** one cell (dir flips dx sign) |
| 4 | 10 | 0 | dx +40 | move **RIGHT** one cell |
| 5,6 | 9 | 0 | ~0 | in-place bob (the `4fd3` stuck fallback) |
| 7,8 | 10 | 1/0 | 0 | in-place shuffle (board 2's spawn id) then decide |
| 9 | 14 | 0 | 0 | long in-place animation |

Cell spacing is the playfield's **40×32 px** (10×4 / 8×4). `4c14` (per *active*
frame): `8560 = kf.frame`; `79ba += (9d2f ? −dx : dx)`; `79bc += dy`; advance; `a1b0--`;
at 0 reset `8563=0` else `8563++`. The entity steps **every other frame** — `4c14`
toggles `8243` and only steps when it became non-zero (≈17.5 Hz over the 35 Hz loop).

### AI navigation (`4c99`, Confirmed)

Runs only on active frames (`8243 != 0 && 8571 != 0xff`). When `a1b0 == 0` (arrived
at a cell) it computes 4 free-flags over the live grid `a0d8` and dispatches:

- **UP** `a0e0` free ⇔ `cell>7 && A[cell-8]==0`
- **DOWN** `a0e1` free ⇔ `cell<0x28 && A[cell]==0` (reads its **own** cell's plane A —
  a faithful oddity, transcribed literally)
- **LEFT** `a0e2` free ⇔ `col!=0 && B[cell-1]==0 && A[cell-1]!=0x0b`
- **RIGHT** `a1b2` free ⇔ `col!=7 && B[cell+1]==0 && A[cell+1]!=0x0b`

All four blocked → `4fd3` (random script `5 + (79b3&3) + (rng&1)`, ids 5–8). Else
`(*0x870[8562])()` — the **on-arrival** dispatch. While moving (`a1b0 != 0`),
`5003`: at `8563 == 5` (visual mid-step) call `(*0x85c[8562])()` to flip the cell
index (`5025` up −8 / `503f` down +8 / `5059` left −1 / `506f` right +1).

`0x870` on-arrival: id 1→`4dbf`, 2→`4e44`, 3→`4ec9`, 4→`4f4e` (0/≥5 → noop `7111`).
`0x85c` mid-step: id 1→`5025`, 2→`503f`, 3→`5059`, 4→`506f` (else noop). Each
on-arrival routine keeps the current direction if free, else turns by a fixed
preference (`4dbf`: up→right→left→down, `4e44`: down→left→right→up, `4ec9`:
left→up→down→right, `4f4e`: right→down→up→left), committing through a leaf
(`4dfa`/`4e7f`/`4f04`/`4f89`) that calls `4bc6(1..4)`. The leaf adds a random
detour **only when `79b3 < 7920`** — so a **sub-type-0 board (board 2) is fully
deterministic** (the leaves always commit their primary direction).

### Collision → death (`5085`/`50c0`/`50fb`, Confirmed)

- `5085` (gated `a0ce==0`): ball AABB `[9290-5, 9290+6] × [9292-5, 9292+5]` (`084c..0852`).
- `50c0` (gated `a0ce==0`): entity AABB `[79ba-5, 79ba+6] × [79bc-5, 79bc+5]` (`0854..085a`).
- `50fb` (gated `8571!=-1 && a0ce==0 && 856d==0 && 792c!=0x30`): standard box overlap →
  `a1aa=1` + `6e11(3)` (death sfx, stubbed). Otherwise `a1aa=0`.

`a1aa=1` is consumed by `1d26` the next frame → `228d` (`a0ce=1, 792a=0, a1aa=0,
4263(0x2e)`) → the **already-ported** state-`0x2e` fly-around death cascade
(`22d2` ×3 → `22fc` lose-a-life). **The death path is shared with the spike death.**

### Render (`1cea`, Confirmed)

When `8571 != -1`, draw bank frame `a0de + 8560` (board 2: `0x1f7..0x1fa`) centred
on `(79ba, 79bc)` — same centre-on-anchor blit as the ball.

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
  also baked to C++ in `src/game/move_scripts.gen.cpp`). Confirmed the playfield
  cell spacing is **40px x 32px**: every move script's net displacement is a whole
  number of cells.
- ~~**Dispatch tables** `DS:0x7ca` + `DS:0x43c0`~~ **DONE** — both resolved by
  `tools/re/dump_player_dispatch.py` and documented above (decide table + the
  animation-step table + the `4437` input tree).
- ~~**Neighbour-reaction tables** for the hop handlers~~ **DONE** — `2634`/`26a1`/
  `270c`/`2776` read `DS:0x4256/0x4276/0x4296/0x42b6[plane-B of the neighbour]`;
  the looked-up code becomes the next state (code `1` ⇒ auto-roll-left `0x01`, with
  a `7921==0x0b` special → `0x16`). Baked into `level_game.cpp` (`kNeigh*`).
- ~~**Tile bump/spring animations**~~ **DONE** — the per-tile recoil system
  (`0x3cda` held-bump, `0x3d0a` idle spring, `0x76b` fall-routing spring,
  `0x35be..0x369e` per-step bump sprites, the descriptor/stream tables
  `0x2ede/0x3256`, and the near-pointer record tables `0x3d6a` (layer A) / `0x40a6`
  → `0x3ad2` (layer B)). Extracted by
  `tools/re/dump_object_anim.py` → `src/game/object_anim.gen.cpp`, ported to
  `LevelGame` + `draw_object_anims`. See the "Tile bump/spring animations" section
  above. The `0x4396` (`6d26` structure trigger) is now wired too (**fixed
  2026-06-24**): `f_6d26` looks up `kStructTrigger[7921]` and calls `f_6d94` →
  `f_69aa`, the call the port had originally dropped (a wrong "only plays a sound"
  comment). Symptom: world-1 **node 14**'s row of special bumpers (`0x14`/`0x15`)
  flung the ball but never sprang back; now they recoil (events `0x2d`/`0x2e`).
  Still open: the `0x266e/0x269e/0x260e/0x263e/0x276e` sfx tables (sfx stubbed).
- **Port progress (Stage 3):** `move_scripts`, `tile_reactions`, `ball_motion`,
  the full `LevelGame` decide/animate dispatch, and the tile bump/spring
  animations are ported + tested + integrated into `App` + SDL. The roll-chaining
  (idle-blink ↔ `4437` input tree ↔ `a1a7` held-bump) and the springs are verified
  by-eye against the original (the lane recoils under the ball). Remaining polish:
  entity AI (`DS:0x870`) for board-3 monsters.
- ~~**Layer-B static draw** (`+0xf1` frame bias + `DS:0x3f4` position)~~ **DONE
  2026-06-24.** The earlier note "world 1 has no blocks" was wrong — D1 boards 3,4,6,10
  (nodes 4,5,7,11) carry plane-B blocks. The static layer-B draw was doubly broken: it
  omitted the **+0xf1 frame bias** (`FUN_1000_17c7`: `*(slot+10)+0xf1`) so blocks drew a
  wrong (low) bank frame, and it used layer A's position table `DS:0xf4` instead of
  layer B's **distinct** `DS:0x3f4` (`x=32+col*40, y=row*32`, offset +32/−24 from A).
  Symptom: node 5's vertical spike walls (plane-B `0x0c`) drew as a horizontal bar in
  the wrong spot. Fixed by baking `+0xf1` into `kLayerB` and adding
  `entity_layer_b_position` (`src/resources/entity_sprites`); `draw_bum_entities` and
  `draw_object_anims` now use it for layer B. Verified by-eye vs `bumpy_005.png`.
- ~~**Spike death** (the "ball flies around the screen and dies")~~ **DONE 2026-06-24.**
  The vertical spike (plane-B `0x0c`) kills via the roll animation-step micro-ops
  `FUN_1000_6326` (roll-left, checks plane-B of cell−1) / `FUN_1000_6372` (roll-right,
  checks plane-B of the cell): a `0x0c` there sets `a0ce=1`, `792a=0` and arms **state
  `0x2e`** (the 26-step fly-around `kSteps_2e`), exactly like the entity hit `228d` but
  with no `a1aa`. State `0x2e`'s decide slot is `FUN_1000_22d2` (the cascade): each time
  the tumble script finishes it bumps `a0ce`; on the 3rd it calls `22fc` (lose a life).
  This needs **no entity** — node 5 has none yet spikes kill. The port had `6326`/`6372`
  stubbed (fell through `anim_dispatch` default) and mis-wired decide `0x22d2`→`f_228d`
  (which would have looped forever); both fixed in `LevelGame` + tested.
- ~~**Tile-value semantics**~~ **DONE** — confirmed against decoded D1 in
  [tile-semantics.md](tile-semantics.md): plane-A behavior is table-driven by
  structure code (5 reaction tables at `DS:0x36be..0x377e` + sprite map
  `0x3d3a`), not fixed constants; the win count `a0cf` excludes free pips/lives.
  (Decoder `tools/re/dump_tile_tables.py`.)
- ~~**Entity AI table** at `DS:0x870` (indexed by `8562`) — the monster movement.~~
  **DONE 2026-06-27** — see "Moving entity" above. The whole monster (spawn
  `2a78`/`48a9`, movement scripts `DS:0x2520`/`4bc6`/`4c14`, maze AI `4c99` +
  `0x870`/`0x85c` + the `4dbf`/`4e44`/`4ec9`/`4f4e` routines, the `4fd3` fallback,
  the cell primitives `5025`/`503f`/`5059`/`506f`, and the AABB collision
  `5085`/`50c0`/`50fb`→`a1aa`→shared state-`0x2e` death) is ported to `LevelGame`
  and rendered by `draw_monster`. Data tables baked by `tools/re/dump_entity_ai.py`.
  Validated on D1 board 2 (node 3) by tests + by eye: the orange creature
  (frames `0x1f7..0x1fa`) spawns at cell 39, animates, walks its row, and kills the
  ball on contact (−1 life). Only board index 2 has an entity in world 1.
- The two foreground blits go through the overlay raster core `0x1cec`
  (unrecovered) — the port already has its own blitter, so this only matters for
  pixel-exact comparison, not for a playable port.

## Worlds 2+ elements: nests, block-top riding, the picture puzzle, entry drop (2026-07-03)

Recovered while auditing per-element parity across all 9 worlds (the systems
below are unreachable in world 1, which is why the world-1 port looked complete).
All are ported to `LevelGame` and covered by `tests/cpp/level_game_test.cpp`.

### Board-entry drop — `FUN_1000_31de` (Confirmed)

Board entry is `FUN_1000_0bf9` = `32b0` (copy grid) → `2a78` (spawn) →
**`31de`**: reset the per-board globals, then start the ball **12px above its
start cell** (`9292 -= 12`) playing the raw 10-step script at **DS:0x1394**
(`{frame 0, dy: -1,-1,0,0,1,1,2,3,3,4}`, armed directly: `a1ac=0x1394`,
`824d=10`, `9bae=4`, `792a=4`) — the drop-in materialize. `31de` also rewinds
the picture-puzzle list (`9ba6 = DS:0x886`, `[0] = 0xff`, `79b7 = 0`).

### The nest, tile `0x16` — `FUN_1000_4305`/`4361`/`495c`/`4995` (Confirmed)

Tile `0x16` is a **nest** the ball parks in. Idle decide (`28f9`) and the
input-tree leaf `440c` route it to `4305`: `792c = 0x1c` (scriptless) + `4361`.
While parked, `4361` cycles the ball frame through the word table **DS:0x1b70**
(`7 6 6 5 5 6 6 7 0 1 2 2 3 3 2 2 1 0 0 0 0`, wrap 0x15) every 4 frames via
`495c`/`4995` (counters `855d`/`a0dc`, frame → `824a`). Exits (`4344`):
fire+left/right hops out (`431b` → `2634`/`26a1`); fire alone keeps spinning;
plain up enters the vertical-hop chain (`4398` → `4454`, state `0x1d`).

**Digging**: in the vertical-hop states `0x1d..0x20`, no-direction routes to
`440c`; on a non-nest tile it calls `6d94(0x2f)` — layer-A event `0x2f` writes
**tile `0x16` under the ball** (2-frame dig anim, stream `8c 8c ff`), and the
next decide parks in the fresh nest. This is the climb mechanic: hop up from a
nest, dig, repeat. (The port previously treated this call as "settle sfx".)

**Erasing — the cloud MOVES (2026-07-03 fix)**: the chain-move commit
**`45a0`** (used by all four tree directions `4454`/`448a`/`44c0`/`4532` when
the target cell is free) arms **`6d94(0x30)`** when the departure tile is
non-zero — event `0x30` (new_tile `0x00`, dissolve stream `b9 b9 ff`) erases
the tile being left. Erase-at-departure + dig-at-arrival is what makes the
ridden cloud *move with the ball*. The diagonal-hop leaves `450c`/`457a`
likewise call `6d94(0x2f)` first when leaving a NON-nest tile (a perch for the
bonk return). The port had dropped all three `6d94` calls (same class of bug
as the `6d26` structure trigger): every chain move left a stale cloud behind —
the duplicated-cloud report from world 2 (`bumpy_008_port.png`, board 11). An
exhaustive sweep of every `6d94`/`69aa`/`6a89`/`6987` call site in the binary
confirms no other event-arming call is missing from the port.

### Block-top riding — states `0x21..0x2a`, `0x32/0x33` (Confirmed)

Hopping onto a cell whose **plane-B** is `0x08` (slab) or `0x0d` (cushion) —
`kNeigh4256/4276[v] = 0x21/0x22` — lands ON the block:

- decide `0x21` = **`1e5e`**: `8551 == 8` (slab) → chain to `21e7` (walk);
  else (`0x0d`) → state `0x24` (sit). `0x22` = **`1e90`** mirrors (→ `2138` /
  state `0x23`). The `6e11(...)` calls these make are **sounds** (`6e30` is the
  sfx synth; `DAT_689c == 4` selects the AdLib table `0x27ae` via `8a07`).
- decide `0x23`/`0x24` = **`1ec2`/`1f3e`**: sit bobbing — `495c(0xb, 5,
  DS:0x1ca4/0x1cba)` cycles ball frames `8 9 a b a a 9 a a a a`; **DOWN** rolls
  off via **`1f03`/`1f7f`**: reuse roll state 1/2 with the raw 4-step script
  **DS:0x140c/0x1460** (`{7,4,2},{7,2,3},{0,2,3},{0,2,4}` / frame 1 variant;
  `9bae=9` mirrors the left roll, `792a=9` so the roll row's pickup steps run),
  springing the seat block (layer-B event `0x16`) — `8570 = 856e` (state 0x23)
  or `856e - 1` (0x24), matching which plane-B slot the ball sat on.
- decide `0x25`/`0x26` = **`2138`/`21e7`**: walk left/right along block tops.
  fire|down (`8244 & 0x12`) smashes down through (state `0x32`/`0x33`); at
  column 0/7 hop off the edge (`8551 = 0x1f`, state `0x27`/`0x28`); otherwise
  the next state comes from **DS:0x42d6/0x42f6** indexed by the neighbour /
  current cell's plane-B value, where the `0x25`/`0x26` sentinel re-checks
  plane-A via **`21bb`/`2261`** (`7921 == 0x0b` → state `0x29`/`0x2a`).
  `2138` zeroes `8551` on entry; `21e7` does not (original asymmetry).

### Picture-block match puzzle — plane-B `0x0e..0x11` (Confirmed)

Bumping a picture block cycles its art through the ordinary layer-B bump events
(`0x0e→0x0f→0x10→0x11→0x0e`, events `0x0b..0x0e`). The block-bump anim step
**`640c`** (states `0x12..0x15`, `0x27/0x28`, `0x34/0x35`, `0x38/0x39`) plays a
per-block sound and, for a pre-bump value in `0x0e..0x11`, calls **`6183`**
(recovered from raw bytes at file `0x7213`; Ghidra's decompiler fails on it):

1. rewind/clear the list at **DS:0x886** (cursor `9ba6`, terminator `0xff`) —
   so any bump stops a running cascade;
2. find the first plane-B value in `0x0e..0x11`; none → return;
3. if any OTHER value in that range differs → return (still mixed);
4. all pictures match: collect every cell with plane-B `0x05` into the list,
   write the `0xff` terminator (skipped if the slot already holds a stale
   `0xff` — original quirk), rewind the cursor, `79b7 = 0`.

**`629c`** (called every main-loop frame between `1349` and `1d26`) pops one
listed cell per 11 frames: `8570 = list[cursor++]`, layer-B event **`0x18`**
(new_tile `0x00` — the `0x05` block opens/vanishes), delay `79b7 = 10`.
At the terminator it rewinds and re-clears the list.

### Misc

- `FUN_1000_6305`/`64c1`/`645d` (anim steps) are sound-only; `673a` is empty.
  They stay no-ops in the port.
- The monster sub-type detours (`79b3 < 7920`, header `0x95` ≠ 0: values
  61/100/102/201/255 across worlds 2-9) were already ported.
- `FUN_1000_7ab4(0x19)` → `49d7` in the main loop is a key check (scancode
  `0x19` = P — pause), not element behavior; not ported yet.

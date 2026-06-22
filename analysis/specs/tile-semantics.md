# Tile semantics (Stage 3, confirmed against decoded D1)

Recovered 2026-06-22 by decoding `D1.BUM` with the port's tested VEC decoder
(`bumpy_port --decode-vec`) and cross-checking the board bytes against the
ground-truth handlers and reaction tables in
`analysis/generated/decomp/all_functions.c` /  `BUMPY.UNPACKED.EXE`. Reproduce
with `tools/re/dump_tile_tables.py`. This **corrects** the earlier guessed tile
constants in [game-loop.md](game-loop.md): tile behavior is **table-driven by
structure code**, not a fixed `0x0e=floor / 0x16=pipe` mapping.

## The board grid (confirmed)

`FUN_1000_32b0` copies the 0x90-byte board record into the live grid `a0d8`
**unchanged** (no load-time transform — verified: the handlers compare the raw
record bytes). It is three 48-byte planes over a 6-row x 8-col cell grid:

| Plane | a0d8 offset | role |
|---|---|---|
| A | `0x00`-`0x2f` | **structure** the ball rolls on / bounces off (pegs, lanes, holes, bumpers) |
| B | `0x30`-`0x5f` | blocks (col 7 unused) |
| C | `0x60`-`0x8f` | **collectibles** |

Cell index `c` (0..47) = `row*8 + col`. Pixel position of a cell comes from the
table at `DS:0x274` (`x = word[c*4+0]`, `y = word[c*4+2]`; ball draws at `x+7,
y+15`, `FUN_1000_4906`). The ball's current cell is `DAT_856e`
(= record byte `0x90` minus 1).

## Plane C — collectibles (confirmed)

Each non-zero cell draws collectible sprite **`frame = value + 0x179`**
(`FUN_1000_2a78`) at the `DS:0x274` position. Collecting (`FUN_1000_6c14` /
`6c95`) clears the cell and scores by value:

| Plane-C value | effect |
|---|---|
| `0x23` `'#'` | **+1 life** (and +250); does **not** count toward the exit total |
| `0x01` | +250; does **not** count toward the exit total |
| `0x2f` `'/'` | +10000 |
| `0x30` `'0'` | +50000 |
| any other | +250 |

**Win condition (confirmed for all 15 D1 boards):** the required count
`DAT_a0cf` = record byte `0x92` = number of plane-C cells whose value is **not
`0x01` and not `0x23`**. Each such pickup decrements `a0cf`; at zero the board is
complete. (`'#'` extra-lives and `0x01` pips are "free" — they score but are not
required.) D1 plane-C values seen: `0x02..0x2f`, dominated by `0x1a` (19x, the
common gem), `0x23` (14x, extra lives), `0x24` (9x).

## Plane A — structure & ball reaction (confirmed mechanism + D1 data)

When the ball sits on a cell it reads the plane-A value into `DAT_7924`
(`FUN_1000_236f`). A few values are handled directly when there is no up/down
input (`FUN_1000_2965` / `28f9`):

| Plane-A value (on-tile) | handler | meaning | in D1? |
|---|---|---|---|
| `0x0f` | `4802` -> state `0xe` | **hole**: fall through, warp to the next hole | yes (6x) |
| `0x0a` | `47cb` | **special lane**: idle reaction is state-indexed (`0x377e[state]`); up/down still hop | yes (39x) |
| `0x16` | `4305` -> state `0x1c` | **pipe**: enter pipe | no in world 1 |
| `0x03` | `463d` | settle then route | no in world 1 |
| `0x20` `' '` | (collect sfx) | — | no in world 1 |
| `0x0e` (cell **above**) | roll check | "blocked above" marker during roll (`23b6`/`29a6`) | no in world 1 |

Everything else goes through five 48-byte **reaction tables** indexed by the
plane-A value, looked up by direction and fed to `FUN_1000_46bb`:

- `DS:0x36be` — no vertical input (`FUN_1000_465e`)
- `DS:0x36ee` — UP pressed (`FUN_1000_467d`)
- `DS:0x371e` — DOWN pressed (`FUN_1000_469c`)
- `DS:0x374e` — indexed by the **cell above** during a roll (`FUN_1000_4747`)
- `DS:0x377e` — on-`0x0a` state-indexed variant (`FUN_1000_47cb`)

`46bb` maps the looked-up code to an action: `0`=roll, `1`=hop up-left
(`2634`), `2`=hop up-right (`26a1`), `3`=fall (`27de`), `8`=`270c`, `9`=`2776`,
`0x1a`=`1fbe`, `0x1b`=`207d`, **any other value = that code becomes the new
player state** (`4263(code)` -> scripted move; see
[move-scripts.md](move-scripts.md)). The plane-A value also selects the drawn
sprite via `DS:0x3d3a[value]`.

Decoded for every plane-A value that occurs in D1 (`tools/re/dump_tile_tables.py`):

```
val  | none        up          down        rollAbove   | sprite | D1 count
0x01 | ROLL        hop-UL      hop-UR      ROLL        | 0x01   | 124   basic lane
0x02 | ROLL        hop-UL      hop-UR      ROLL        | 0x8d   |  14   lane (alt art)
0x05 | hop-UL      hop-UL      hop-UL      ->state0x11 | 0x10   |   5   left deflector
0x06 | hop-UR      hop-UR      hop-UR      ->state0x11 | 0x12   |   2   right deflector
0x07 | ROLL        hop-UL      hop-UR      ROLL        | 0x14   |   8   lane
0x08 | ROLL        hop-UL      hop-UR      ROLL        | 0x1d   |  14   lane
0x09 | ROLL        hop-UL      hop-UR      ROLL        | 0x26   |   9   lane
0x0a | ROLL        hop-UL      hop-UR      ROLL        | 0x32   |  39   special lane (47cb)
0x0f | ->state0xe  hop-UL      hop-UR      ROLL        | 0x60   |   6   HOLE
0x10 | ROLL        hop-UL      hop-UR      ROLL        | 0x64   |   5   lane
0x12 | ->state0x10 ->state0x10 ->state0x10 ->state0x11 | 0x74   |  14   trigger state 0x10
0x14 | special1fbe special1fbe special1fbe ->state0x11 | 0x85   |   6   special bumper
0x15 | special207d special207d special207d ->state0x11 | 0x8b   |   9   special bumper
0x19 | FALL        FALL        FALL        ROLL        | 0x4b   |   7   fall-through bumper
0x1e | ROLL        hop-UL      hop-UR      ->state0x11 | 0x9d   |  20   lane (ceiling above)
0x1f | ->state0x10 ->state0x10 ->state0x10 ROLL        | 0xa4   |  15   trigger state 0x10
```

### Reading the table

- **Lanes** (`0x01,0x02,0x07,0x08,0x09,0x0a,0x10,0x1e`): the ball rolls along
  them; UP hops up-left, DOWN hops up-right (the core BUMPY input). Different
  values are just different lane art (sprite column). `0x01` is by far the most
  common.
- **Deflectors** (`0x05` left, `0x06` right): always hop the same way regardless
  of input — fixed-direction bumpers.
- **`0x0f` = hole**: with no vertical input the ball drops through and warps
  (state `0xe`).
- **`0x19`**: always falls — a pass-through/trap bumper.
- **`0x12`/`0x1f`**: any horizontal contact triggers state `0x10`; **`0x14`/
  `0x15`** trigger the `1fbe`/`207d` special bounces. The exact visual/effect of
  state `0x10` and `1fbe`/`207d` is the remaining follow-up (Hypothesis: special
  bumpers / board-exit), but the trigger mapping is confirmed.
- **`rollAbove` column** (`0x374e[value]`): when this value is the cell *above*
  the rolling ball, `0x11` means "ceiling — go to state `0x11`" (bounce off the
  ceiling) and `0x00` means "open — keep rolling".

## Corrections to earlier notes

- game-loop.md's "tile-value semantics (`0x0e` floor, `0x16` pipe, ...)" was a
  Hypothesis from handler literals; against real D1 data the dominant lane is
  `0x01`, `0x0e`/`0x16`/`0x03`/`0x20` do **not** occur in world 1, and behavior
  is table-driven (above). Pipe/settle/space tiles appear only in later worlds.
- The collectible count `a0cf` is **not** "all plane-C cells" — it excludes the
  free `0x01` pips and `0x23` extra-lives (confirmed across all 15 boards).

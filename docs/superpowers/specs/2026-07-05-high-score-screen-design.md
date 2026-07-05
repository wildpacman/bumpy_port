# High-score screen — design

Date: 2026-07-05. Status: approved for planning.

## Goal

Add the original's **HIGH-SCORE screen** to the port, reachable the two ways the
original allows:

1. **Menu row 1** (`HIGH SCORE`) — view the table, any key returns to the menu.
2. **On Game Over** (all lives lost) — show a brief `GAME OVER` screen, then the
   high-score table; if the run's final score beats an entry, run the interactive
   **name-entry editor**, then return to the menu.

Faithful to the original in look, flow, and behavior. Decided with the project
owner: **do the whole faithful flow**, including the `GAME OVER` screen and the
name-entry editor, with **no disk persistence** (the table is baked defaults,
held in memory for the session, and resets each launch — exactly like the
original's data-segment table).

Non-goals: the PASSWORD sub-screens (`0f7a`/`0d9d`/`5c87`), disk persistence, and
any high-score after *victory* (the original does not do this — clearing world 9
goes outro → menu, which the port already has).

## Recovered behavior (evidence)

All addresses are `segment:offset` in `analysis/generated/decomp/all_functions.c`;
data-segment offsets resolve in `BUMPY.UNPACKED.EXE` at file offset
`0x11440 + DS_offset` (data segment `0x103b`). This screen was previously
undocumented; `PROJECT_STATUS.md` mislabeled `0d9d/0f7a/11eb` as "HIGH-SCORE" —
those are the PASSWORD/GAME-OVER screens. The high-score table is
`FUN_1000_5681` → `FUN_1000_57e1`, with name entry `FUN_1000_59d3`.

### Assets and data

- **Background:** `SCORE.VEC` (MENU resource table `DS:0x0928`, **index 3**,
  size `0x7d63` = 32099 bytes). It is a standard uncompressed **320×200
  screen-format** image (99-byte header, 16-colour VGA palette at offset 51, four
  8000-byte plane-sequential bit-planes) — the same format as `TITRE`/`MONDE`/
  `DESSFIN`, already handled by `src/video/screen_image`.
- **Table:** 7 records × 8 bytes at `DS:0x8f0` (file `0x11D30`). Record layout:
  `+0 u16` name offset (near, in DS), `+2 u16` name segment, `+4 u16` score low,
  `+6 u16` score high. Score is the 32-bit `high<<16 | low`. Names are 8 chars
  from `{'.', '0'-'9', 'A'-'Z'}`, `'.'` = blank/pad.
- **Baked defaults** (verified from the binary; port seeds these):

  | # | Name (8 ch) | Score |
  |---|---|---|
  | 1 | `BIG JIM.` | 5,000,000 |
  | 2 | `SUPER JO` | 3,000,000 |
  | 3 | `STEVE...` | 1,000,000 |
  | 4 | `WILIAM..` |   200,000 |
  | 5 | `JOHNNY..` |    30,000 |
  | 6 | `FRANK...` |     4,000 |
  | 7 | `MIKE....` |       500 |

- **No persistence.** The only file I/O in the image is the read-only resource
  loader (`FUN_1000_736f` → `FUN_1000_a21c`). There is no DOS create/write
  anywhere and no score-data file among the assets. The table lives in the data
  segment and resets to the defaults every launch.

### Text glyphs (BUMSPJEU sprites, not the DDFNT2 HUD font)

Names and scores are drawn as **BUMSPJEU.BIN sprite frames** via the command
block `DS:0x792e` (`FUN_1000_942a(0x792e)`), *not* the `DDFNT2.CAR` HUD font used
on the map. The glyph sheet packs, contiguously: `'0'-'9'` = frames
`0x1ac..0x1b5`, `'A'-'Z'` = `0x1b6..0x1cf`, `'['` (caret) = `0x1d0`; `'.'` =
`0x1a3` (separate, a blank). All glyph frames have **origin (0,0)** and are 16 px
wide, so the blitter draws each **top-left at the literal command (x, y)** (unlike
the centre-anchored ball/marker frames).

Char → frame mapping used by the port renderer:
- `'0'..'9'` → `0x1ac + (c - '0')`  (equivalently `ascii + 0x17c`)
- `'A'..'Z'` → `0x1b6 + (c - 'A')`  (equivalently `ascii + 0x175`)
- `'['` → `0x1d0`;  `'.'` / space → blank (not drawn)

### Table render — `FUN_1000_57e1`

Loop over 7 rows (`row = 0..6`), record pointer `DS:(row*8 + 0x8f0)`:

- **Insert test** (`:6784`): if `player_score > entry.score` and nothing inserted
  yet, shift rows `[row..5]` down one, set the new row's name to 8×`'A'`, write the
  player's score, and remember `insert_row = row`. Strict `>` ("beats").
- **Name** (`:6811`): 8 glyph cells at `x = col*16` (0,16,…,112),
  `y = row*16 + 0x41` (65,81,…,161). Each char → its glyph frame; `'.'`/space →
  blank. On the freshly-inserted row, an empty/`'.'` cell shows `'['` (the caret).
- **Score** (`:6829`, `FUN_1000_603d`): 7 zero-padded digits at `x = 0xb0 + i*16`
  (176,192,…,272), same `y`, digit → frame `0x1ac + digit`.
- **Exit**: if the player did not qualify (score didn't beat row 7 = 500) →
  `FUN_1000_328f()` = clear latch + spin until any key. If they qualified →
  `FUN_1000_59d3(insert_row)` (name entry).

From the **menu**, `player_score` is 0, so nothing qualifies → view-only, exits on
any key.

### GAME OVER — `FUN_1000_11eb`

- Loads resource index 3 (`FUN_1000_736f(3)`) and calls **`FUN_1000_3467` (the
  edge-to-centre darken)**, but — unlike the high-score screen `FUN_1000_5681`,
  which deplanes SCORE.VEC via **`FUN_1000_7b5a`** — it **never deplanes the image
  into the buffer** (no `7b5a` call, line 1452 vs 6714). So the visible result is
  **"GAME OVER" text on the darkened BLACK screen**, not the HALL OF FAME art.
  (Confirmed against the original by the project owner.) The port keeps SCORE.VEC's
  palette (so the glyph colour resolves) but clears to black.
- Overlays `"GAME OVER"` (9 chars, `DS:0x1327`) with the same glyph frames
  (`ascii + 0x175`) at **column start 6**, `y = 0x60 (96)`, `x = col*16`
  (cells at x = 96,112,…,224; the space at index 4 is skipped).
- **Waits a fixed, un-paced delay — NOT a keypress**: `FUN_1000_3e74()` ×2 =
  `FUN_1000_05e7(0x32)` ×2 = 100 buffer-commits (a short sub-second flash), then
  returns. (Contrast the inter-world `0d9d` which waits for fire.)

### Death-path sequencing — `FUN_1000_0c18`

The `DAT_928d == -1` (`0xff`, set by `FUN_1000_22fc:2876` when lives hit 0) branch
(`:1246-1248`) runs **`FUN_1000_11eb()` then `FUN_1000_5681()` back-to-back**,
nothing between. So the visible sequence is a **two-darken flow**: the level darkens
to the GAME OVER screen, holds briefly, then `5681` reloads `SCORE.VEC` and darkens
again into the table. `DAT_a0d4/a0d6` (score) is **reset only** at the menu-restart
label `LAB_0c2c` (`:1165`), never on this path, so the insert test sees the live
final score.

### Name entry — `FUN_1000_59d3`

**Continuous / held-repeat, not edge-triggered.** The loop polls the live held-key
mask (`FUN_1000_75a2`, ORs the key-state table) with **no release latch**, acting
on whatever is held each iteration, paced only by a per-action delay
(`FUN_1000_5fdb` ends in `FUN_1000_05e7(8)`; the idle-blink branch uses
`05e7(1)`). Explicitly unlike the menu loop `35a5`, which debounces to full
release.

- **Up/Down**: cycle the glyph at the caret through frames `0x1ad..0x1cf` (chars
  `'1'-'9'`, `'A'-'Z'`) with the `0x1d0 → 0x1a3` wrap; the stored name byte is
  `glyph - 0x175` (ASCII).
- **Left/Right**: move the caret across columns; bounds are `col == 0` (left stop)
  and `col > 6` blocks a further right — so the caret ranges **0..7, all 8
  characters editable**.
- **Fire (`0x10`)**: confirms on **press**; the loop exits and commits.
- Idle (no key): the caret column blinks (draw/erase toggled by a counter).

## Port design

New units, following the existing `WorldMap`/`screen_image`/`hud` patterns. Game
logic stays SDL-free; the shell renders and paces.

### `src/game/high_scores.{h,cpp}` — the session table (pure)

```cpp
struct HighScoreEntry { std::array<char, 8> name; std::uint32_t score; };

class HighScoreTable {
public:
    HighScoreTable();                       // seed the 7 baked defaults
    std::span<const HighScoreEntry> entries() const;
    int qualifies(std::uint32_t score) const;   // insert row (0..6) or -1; strict '>'
    int insert(std::uint32_t score);            // shift down, name = 8×'A', return row
};
```

No file I/O. One instance lives in `App` for the whole session, so a name entered
in one game shows in a later game's table (matching the data-segment table).

**Name character encoding.** The port stores each name cell as its **displayed
ASCII char** (`'.'`/space, `'0'-'9'`, `'A'-'Z'`) and renders via the char → frame
table above. This is readable (the defaults are literal ASCII) and round-trips
cleanly for the letters + `'.'` the alphabet uses. The original instead stores a
frame-relative code (`frame - 0x175`), which coincides with ASCII for letters/`'.'`
but not for the digit-region frames; the port avoids that quirk by keying render
and edit off the displayed char, not the raw code. Behaviorally equivalent for the
`{'.', '0'-'9', 'A'-'Z'}` alphabet.

### `src/game/high_score_screen.{h,cpp}` — the screen state machine (pure)

Delegated to by `App`, exactly like `WorldMap`. Owns the transient screen state;
mutates the caller-owned `HighScoreTable` only through it.

- Modes: `view` (from the menu) and `entry` (from Game Over). `enter(mode, table)`.
  In `entry` mode it calls `table.qualifies/insert` and, if inserted, drives the
  editor over the inserted row.
- State: `insert_row` (-1 if none / view mode), `cursor_col` (0..7), a blink
  counter, and release/repeat guards.
- `update(input) -> {none | done}`:
  - **view**, or **entry with no qualifying score**: any key → `done` (mirrors
    `328f`). A release guard swallows the key that caused entry (the fire that won
    nothing, or the death key) so it can't instantly dismiss.
  - **entry with a caret**: held-repeat editing — Up/Down cycle the glyph at
    `cursor_col`, Left/Right move the caret (0..7), Fire commits → `done`. Held
    keys repeat, gated by a per-action frame delay (`kNameRepeatFrames`, a pacing
    constant tuned by eye against the original; the original's clock is buffer
    commits, not retraces, so the exact rate is pinned visually). A fire-release
    guard on entry prevents an instant commit (the `59d3` hold quirk).
- `view()` exposes `{mode, insert_row, cursor_col, caret_visible}` for the renderer.

### `src/video/high_score_renderer.{h,cpp}`

```cpp
void render_high_scores(std::span<const std::uint8_t> score_vec,
                        const HighScoreTable& table,
                        std::span<const std::uint8_t> sprite_bank,
                        const HighScoreScreenView& view,
                        IndexedFramebuffer& target);

void render_game_over(std::span<const std::uint8_t> score_vec,
                      std::span<const std::uint8_t> sprite_bank,
                      IndexedFramebuffer& target);
```

- Both call `apply_screen_image_palette(score_vec)` + `draw_screen_image(score_vec)`
  for the shared background.
- A small internal `draw_glyph_string(text, x, y, sprite_bank, target)` maps each
  char → its BUMSPJEU frame (table above) and blits via the existing
  `decode_sprite_frame` + top-left blit (the `hud.cpp` `blit_sprite` convention,
  factored/shared). `'.'`/space skipped.
- `render_high_scores`: per row, name at `(col*16, row*16+65)` and 7 score digits
  at `(176 + i*16, row*16+65)`; on `insert_row`, the caret column draws `'['` when
  `caret_visible`.
- `render_game_over`: `"GAME OVER"` at `(96 + i*16, 96)`.

### `App` integration (`src/game/app.{h,cpp}`)

- New `Screen::game_over` and `Screen::high_scores`. `MenuAction::high_scores`.
- `App` owns `HighScoreTable high_scores_` (session-lifetime) and
  `HighScoreScreen high_score_screen_`.
- **Menu row 1** (currently a no-op in `menu.cpp`): `Menu::update` returns
  `MenuAction::high_scores`; `App` enters `high_score_screen_` in `view` mode and
  sets `Screen::high_scores`. `done` → `Screen::menu`.
- **Game Over**: `finish_level(quit, …)` (= out of lives; today it does
  `reset_run()` + menu) instead sets `Screen::game_over`, keeping the run's final
  `score_`. (Verified: `LevelGame` sets `d_928d = 0xff` → `LevelStatus::quit` at
  exactly one site — when `d_791a` (lives) hits 0 in the death routine — so `quit`
  is *only* the game-over path and rerouting it cannot misfire on some other quit.) A frame timer (`kGameOverFrames`) in `App::update` auto-advances
  `game_over → high_scores` (entry mode, seeded with `score_`). When the
  high-score screen reports `done`, `App` calls `reset_run()` → `Screen::menu`.
- Because the shell plays the edge-to-centre darken on every `Screen` change, the
  faithful **two-darken** sequence (level → game_over → high_scores) is automatic.
  The GAME OVER timer only advances on non-darken frames (the shell freezes
  `app.update` while a transition is active), so the hold starts once the screen is
  actually visible.

### Shell (`src/platform_sdl3/sdl_app.cpp`, `src/app/main.cpp`)

- Load `SCORE.VEC` once in `main.cpp` (world-independent, like `DESSFIN.VEC`) and
  pass its decoded bytes into `run()` (extend `SdlApp::run`'s signature alongside
  `outro_screen`).
- Add render branches: `Screen::game_over` → `render_game_over(...)`,
  `Screen::high_scores` → `render_high_scores(...)`. Both pace at the full retrace
  rate (70 Hz, like the menu).
- Feed input into `app.update` as today; the high-score screen consumes it via
  `App` → `HighScoreScreen`.

### Menu row wiring note

The original menu is `0=START, 1=HIGH SCORE, 2=option toggle, 3=PASSWORD`, with
Escape = quit. The port currently maps row 3 → quit (a stand-in) and leaves row 1
a no-op. This change wires **row 1 → HIGH SCORE** and leaves row 3 as-is; PASSWORD
stays out of scope. Row 1's label is already baked into `TITRE.VEC`, so the cursor
lands on the correct on-screen text.

## Pacing constants (tuned by eye)

- `kGameOverFrames` — how long the GAME OVER screen holds before auto-advancing
  (~0.5–0.8 s; the original is ~100 un-paced commits). A named constant in the
  shell/App, like `kDarkenFramesPerRing`.
- `kNameRepeatFrames` — held-repeat cadence for the name editor (the original paces
  by buffer commits, not retraces, so this is pinned visually against DOSBox).

## Testing

- `tests/cpp/high_scores_test.cpp`: default table contents/order; `qualifies`
  (below 500 → -1; beats #7; beats #1; tie does not insert); `insert` (shift-down
  correctness, insert row, name pre-filled `AAAAAAAA`, last entry dropped).
- Name editor (via `HighScoreScreen`): caret bounds 0..7, glyph cycle + wrap,
  fire commits, view-mode any-key dismiss, release guard swallows the entry key.
- `tests/cpp/app_test.cpp`: menu row 1 → high_scores(view) → menu;
  `finish_level(quit)` → game_over → (timer) → high_scores(entry) → done →
  reset_run + menu. Updates the existing `finish_level(quit)` expectation.
- Renderer: glyph-frame mapping (char → frame index) is unit-tested; a
  `--render-highscores SCORE.VEC out.bmp [insert_row]` dev flag (mirrors
  `--render-outro`) dumps the screen for by-eye verification, and
  `--render-gameover SCORE.VEC out.bmp` dumps the GAME OVER screen.

## Definition of done

- From the menu, row 1 shows the `SCORE.VEC` table with the 7 baked entries drawn
  in BUMSPJEU glyphs; any key returns to the menu; the edge-to-centre darken plays
  both ways.
- Losing the last life shows `GAME OVER` (brief), then the table; a qualifying
  final score inserts a row and runs the name editor (held-repeat arrows, fire
  commits); then the run resets and returns to the menu. A non-qualifying game over
  shows the table and dismisses on any key.
- Verified by eye against the original (DOSBox side-by-side) and by the tests
  above; original assets verify clean.

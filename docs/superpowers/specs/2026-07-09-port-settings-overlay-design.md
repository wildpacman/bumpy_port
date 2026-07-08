# Port-settings overlay — design

Date: 2026-07-09
Status: approved (pending spec review)

## Overview

A discoverable, in-game **settings overlay** for the port, opened with **Tab** on
the play surfaces (menu / world map / level). It surfaces the graphics options
that today live only on hotkeys (`Alt+3` 3D, `Alt+A` aspect, `Alt+Enter`
fullscreen), adds audio toggles (music, SFX), a read-only password reference for
all worlds, and a Quit item. It is the "port-settings overlay" deferred as a
separate spec by the 3D-render-mode design
(`docs/superpowers/specs/2026-07-08-3d-render-mode-design.md`, §"Phasing" item 3),
built on the `bumpy_port.cfg` persistence that phase established.

This spec also **changes the launch defaults** so a fresh install boots the way
the game is meant to be shown: fullscreen, 4:3, 3D.

## Goals

- One place to see and change every port presentation option, without memorizing
  hotkeys.
- New launch defaults: **fullscreen + 4:3 + 3D**, persisted and editable.
- Add **music** and **SFX** on/off, persisted.
- A **PASSWORDS** reference page listing the world codes 2–9.
- Look and render exactly like the game's own full-screen text pages
  (GAME OVER / PASSWORD / HIGH SCORES): big sprite-glyph font on the SCORE.VEC
  palette. No new art, no new visual language.

## Non-goals

- No volume slider (out of scope; only on/off toggles).
- No translucent compositing over the live 3D scene (approach B, considered and
  rejected below — the overlay is an opaque full-screen page).
- No changes to gameplay, timing, PRNG, the `App` state machine, or the
  `--render-*` RE dump outputs.
- No new persisted state beyond the two audio flags (high scores stay
  session-only, as in the original).

## Launch defaults and config

`PortConfig` (`src/core/port_config.h`) gains two fields and flips three
defaults:

| key             | old default | new default | meaning                          |
|-----------------|-------------|-------------|----------------------------------|
| `render3d`      | `false`     | **`true`**  | Alt+3 diorama on at launch       |
| `square_pixels` | `true`      | **`false`** | `false` = 4:3 (CRT); `true`=16:10|
| `fullscreen`    | `false`     | **`true`**  | start fullscreen                 |
| `music`         | —           | **`true`**  | new: intro-music gate            |
| `sfx`           | —           | **`true`**  | new: SFX gate                    |

Model chosen by the user: **new defaults, editable and persisted** (not a forced
per-launch reset). The overlay flips these live and writes them back to
`bumpy_port.cfg`; the next launch restores whatever was last saved. `render3d`
still only *arms* when a GL 3.3 context is present (the flag is kept regardless so
a machine upgrade re-enables it), matching the existing `run()` logic.

**Compatibility caveat (must document in-repo):** an *existing* `bumpy_port.cfg`
(e.g. the developer's, written during 3D testing) overrides the new defaults for
its known keys — only the two new keys (`music`, `sfx`) get defaults appended on
next write. To observe a clean first-run (FS + 4:3 + 3D) the old file must be
deleted. `parse_port_config` already leaves unknown/missing keys at their struct
defaults, so no migration code is needed; the new keys parse and serialize with
the same `"1"/"0"` convention as the rest.

## Architecture

The overlay is a **modal layer owned by the SDL shell** (`sdl_app.cpp::run`), not
a new `App::Screen`. Rationale: fullscreen, aspect, 3D, and audio are already
shell concerns — the window handle, the `PortConfig`, the `AudioEngine`, and the
live `render3d`/`square_pixels` variables all live in `run()`. Keeping the overlay
there means **`App` and all game logic are untouched**.

A single `bool overlay_open` gates the run loop the same way the screen-change
darken already does: while open, `app.update()`, `game->tick()`, and the
transition stepper do **not** run — the world is frozen — and input is routed to
the overlay instead. On close, `next_frame` is resynced to
`SDL_GetPerformanceCounter()` so the pause does not produce a catch-up burst.

Availability: Tab opens the overlay only on `Screen::menu`, `Screen::map`, and
`Screen::level`. On other screens (splash, outro, game over, password entry,
high-score entry) Tab is ignored — those have their own input/timers and no
settings use-case.

### Components / files

| File | Change |
|------|--------|
| `src/core/port_config.{h,cpp}` | +`music`,+`sfx` fields; new defaults; parse/serialize the two keys |
| `src/game/settings_overlay.{h,cpp}` | **new**: `SettingsPage`, `SettingsEvent`, `SettingsView`, `SettingsOverlay` model (SDL-independent, unit-tested) |
| `src/video/settings_renderer.{h,cpp}` | **new**: `SettingsRenderer` — draws a `SettingsView` as a full-screen page |
| `src/audio/audio_engine.{h,cpp}` | +`set_sfx_enabled(bool)`; `play_sfx` early-returns when disabled |
| `src/platform_sdl3/sdl_app.{h,cpp}` | overlay state, Tab handling, input routing, render/present, applying toggle events; `run()` gains a `const SettingsRenderer&` param |
| `src/app/main.cpp` | build `SettingsRenderer`; seed audio from `config.music`/`config.sfx`; `--render-options` dump tool |
| `tests/cpp/settings_overlay_test.cpp` | **new**: model tests |
| `tests/cpp/port_config_test.cpp` | extend: new keys + new defaults |
| `tests/cpp/audio_*` | SFX-gate test |
| `CMakeLists.txt` | register the new sources + test |

## Overlay model (`SettingsOverlay`)

SDL-independent, testable like `Menu`. It owns only navigation state; the setting
*values* live in the shell (the `PortConfig`), so there is a single source of
truth.

```cpp
enum class SettingsPage { root, video, audio, passwords };

enum class SettingsEvent {
    none,
    toggle_3d, toggle_aspect, toggle_fullscreen,   // VIDEO page
    toggle_music, toggle_sfx,                        // AUDIO page
    quit,                                            // root QUIT
    close,                                           // back-out of root
};

// Assembled by the shell each frame from the overlay's page/cursor + the live
// PortConfig/GL state; consumed by SettingsRenderer.
struct SettingsView {
    SettingsPage page;
    int cursor_row;
    bool render3d, square_pixels, fullscreen, music, sfx;
    bool render3d_available;   // gl_ != nullptr -> the 3D row is selectable
};

class SettingsOverlay {
public:
    SettingsEvent update(const MenuInput& input, bool render3d_available) noexcept;
    void reset() noexcept;                       // page=root, cursor=0 (on open)
    [[nodiscard]] SettingsPage page() const noexcept;
    [[nodiscard]] int cursor_row() const noexcept;
private:
    SettingsPage page_{SettingsPage::root};
    int cursor_row_{};
    bool waiting_for_release_{};                  // debounce, like Menu
};
```

Input mapping inside `update` (own press-debounce, matching `Menu`/`PasswordScreen`):

- **↑ / ↓** — move the cursor within the current page (wraps). Returns `none`.
- **→ / Enter (confirm)** — activate the current row:
  - root: `VIDEO`/`AUDIO`/`PASSWORDS` switch `page_` (internal, returns `none`);
    `QUIT` returns `quit`.
  - video: returns `toggle_3d` (only if `render3d_available`, else `none`),
    `toggle_aspect`, or `toggle_fullscreen`.
  - audio: returns `toggle_music` or `toggle_sfx`.
  - passwords: read-only, returns `none`.
- **← / Esc (cancel)** — back one level: on a sub-page, `page_=root, cursor_=0`
  (returns `none`); on root, returns `close`.

**Tab is handled by the shell**, not the model: it is the global open/close toggle
(open → `reset()` + `overlay_open=true`; open again → `overlay_open=false`).

The shell acts on the returned event: performs the SDL/audio side effect, updates
the matching `PortConfig` field, and calls the existing `persist()` — one write
per change, exactly as the hotkeys do today. The disabled `toggle_3d` (no GL) is a
no-op.

## Pages (mockups, 16-px glyph grid)

Big glyph font covers `A–Z`, `0–9`, `.` only (no `:`/`/`), so labels are composed
from those. Cursor = the main menu's arrow (`FLECHE.BIN` frame 0), drawn at the
selected row.

Root:
```
              O P T I O N S

        > VIDEO
          AUDIO
          PASSWORDS
          QUIT
```

VIDEO:
```
               V I D E O

        > 3D            ON
          ASPECT        4.3
          FULLSCREEN    ON
```
(`ASPECT` shows `16.10` when `square_pixels`, `4.3` otherwise. The `3D` value is
`ON`/`OFF`; when no GL context exists the row is drawn dimmed and its value reads
`OFF` and cannot be toggled.)

AUDIO:
```
               A U D I O

        > MUSIC         ON
          SOUND         ON
```

PASSWORDS (read-only; derived from `password_code_for_world(2..9)`):
```
            P A S S W O R D S

        2  ACCESS       6  WINNER
        3  BUTTON       7  ZOMBIE
        4  ISLAND       8  LOVELY
        5  PRETTY       9  SYSTEM
```
Back with ← / Esc / Tab.

Exact pixel coordinates (title Y, row step, label/value columns) are an
implementation detail settled in the plan against the existing
`password_renderer` / `high_score_renderer` layout constants.

## Rendering (`SettingsRenderer`) — approach A

Mirrors `render_password`: the page is an **opaque full-screen render on the
SCORE.VEC palette**.

```cpp
class SettingsRenderer {
public:
    // score_vec = raw SCORE.VEC bytes (palette source); sprite_bank = BUMSPJEU
    // (glyphs); cursor_sprite = FLECHE.BIN (arrow marker).
    SettingsRenderer(std::span<const std::uint8_t> score_vec,
                     std::span<const std::uint8_t> sprite_bank,
                     std::span<const std::uint8_t> cursor_sprite);
    void render(const SettingsView& view, IndexedFramebuffer& target) const;
};
```

Per frame while open: `apply_screen_image_palette(score_vec, target)` →
`target.clear(0)` → `draw_glyph_string` for the title, rows, and values → draw the
cursor sprite at the active row. This guarantees the glyphs' baked palette indices
resolve to the correct colours regardless of which screen is underneath, and it
reads as one system with the game's other text pages.

`SettingsRenderer` is constructed in `run_sdl_menu` (like `MenuRenderer`) from
assets already loaded there — `score_screen`, `sprite_bank`, and the FLECHE bytes
(`MenuResources::cursor_sprites`, which already outlive `run()` via the
`MenuRenderer` reference) — and passed into `run()` by const ref.

### Why approach A (and why not B)

The big glyph sprites carry baked palette indices that only look right under the
SCORE.VEC palette (that is why GAME OVER/PASSWORD install it). Showing the live
game *behind* a translucent panel (approach B) would fight that palette, and in 3D
would additionally require a dedicated GL compositing pass and splitting
`present_flat`/`present_3d` into render-then-swap. For a **paused** settings menu
the payoff does not justify the cost. Approach A also makes the 3D case trivial:

**3D handling.** While `overlay_open`, the loop renders the overlay page into
`frame` and presents it through the **flat** path (`present_frame()`, which already
handles both the GL and the SDL_Renderer back-ends). The 3D scene is paused and
hidden behind the opaque page; closing the overlay returns to the diorama. No
`present_3d_level()` call happens while the overlay is open, so there is no
compositing and no palette conflict.

## Audio changes

- **SFX gate.** `AudioEngine` gains `set_sfx_enabled(bool)` (a mutex-guarded
  `sfx_enabled_`, default `true`); `play_sfx` returns early when disabled. The
  shell calls `set_sfx_enabled(config.sfx)` once at startup and again on each
  `toggle_sfx`.
- **Music gate.** No engine change: music already has `start_music`/`stop_music`,
  and the only place it plays is the startup splash. The shell wraps its two
  `start_music()` sites (the pre-loop splash arm and the splash-entry on a screen
  change) in `if (config.music)`. `toggle_music` calls `stop_music()` when turning
  off, and `start_music()` when turning on *and* currently on the splash screen;
  it persists the flag either way.

## Run-loop integration (sketch)

In the SDL event loop, before the existing `update_key_state`/hotkey handling
routes to the app:

```cpp
if (event is KEY_DOWN, key == SDLK_TAB, no repeat) {
    if (overlay_open) { overlay_open = false; next_frame = SDL_GetPerformanceCounter(); }
    else if (screen is menu/map/level) { overlay.reset(); overlay_open = true; }
    // swallow Tab either way
}
```

Then, near the top of the frame body, before `app.update`:

```cpp
if (overlay_open) {
    switch (overlay.update(input, gl_ != nullptr)) {
        case toggle_3d:        /* mirror Alt+3  */ break;
        case toggle_aspect:    /* mirror Alt+A  */ break;
        case toggle_fullscreen:/* mirror Alt+Enter */ break;
        case toggle_music:     /* flip config.music + start/stop; persist */ break;
        case toggle_sfx:       /* flip config.sfx + set_sfx_enabled; persist */ break;
        case quit:             running = false; break;
        case close:            overlay_open = false; next_frame = ...; break;
        case none:             break;
    }
    if (running && overlay_open) {
        settings_renderer.render(build_view(overlay, config, gl_), frame);
        present_frame();
        wait_next_tick(period_full);   // menu rate
    }
    continue;   // freeze the world under the overlay
}
```

The three graphics cases reuse the *exact* side-effect code already behind
`Alt+3`/`Alt+A`/`Alt+Enter`; those hotkeys are **kept** as accelerators (the
overlay is for discoverability, not a replacement).

## Testing

- **`SettingsOverlay` unit (Catch2):** cursor navigation wraps within each page;
  `confirm`/`right` on each root row switches to the right page or returns `quit`;
  `confirm`/`right` on each VIDEO/AUDIO row returns the correct toggle event;
  `toggle_3d` is suppressed when `render3d_available == false`; `cancel`/`left`
  backs out of a sub-page to root and returns `close` from root; press-debounce
  (a held key fires once).
- **`port_config`:** the new defaults; round-trip serialize→parse of `music`/`sfx`;
  an old file missing the two keys parses them as `true`; the three flipped
  defaults hold when the file is absent.
- **Audio:** `play_sfx` produces no active voice after `set_sfx_enabled(false)`,
  and resumes after `set_sfx_enabled(true)`.
- **Renderer by-eye:** `--render-options <SCORE.VEC> <out.bmp> [page]` — an offline
  BMP dump (peer of the existing `--render-*` tools) for each page (root / video /
  audio / passwords), checked by eye.

## Phasing

1. Config: fields, defaults, parse/serialize, tests. (Independently mergeable —
   flips the launch defaults on its own.)
2. `AudioEngine` SFX gate + test.
3. `SettingsOverlay` model + tests.
4. `SettingsRenderer` + `--render-options` dump; by-eye check of all four pages.
5. Shell wiring: Tab, input routing, event application, flat present, keep
   hotkeys; end-to-end check in the running game (2D and 3D).

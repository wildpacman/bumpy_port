# Port-settings overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Tab-opened in-game settings overlay (VIDEO / AUDIO / PASSWORDS / QUIT) and flip the launch defaults to fullscreen + 4:3 + 3D.

**Architecture:** The overlay is a modal layer owned by the SDL shell (`sdl_app.cpp::run`); `App` and game logic are untouched. A pure, unit-tested `SettingsOverlay` model owns page/cursor navigation and emits events; the shell applies each event (SDL/audio side effect + `PortConfig` write). Rendering reuses the game's big sprite-glyph font on the SCORE.VEC palette (approach A: an opaque full-screen page), so the 3D path simply presents flat while the overlay is open.

**Tech Stack:** C++20, SDL3, OpenGL 3.3, Catch2, CMake (preset `windows-debug`).

**Design spec:** `docs/superpowers/specs/2026-07-09-port-settings-overlay-design.md`

## Global Constraints

- C++20; `CMAKE_CXX_EXTENSIONS OFF`; namespace `bumpy`.
- New non-SDL sources go in the `bumpy_core` library (`CMakeLists.txt`); SDL/GL code stays in `bumpy_platform_sdl3`.
- Big glyph font (`draw_glyph_string`) supports only `A–Z`, `0–9`, `.` — compose all labels from those (no `:` or `/`).
- Config persistence uses the existing `"1"/"0"` key=value convention; unknown/missing keys stay at struct defaults.
- Tests are Catch2 (`#include <catch2/catch_test_macros.hpp>`), run from the repo root (asset files load by relative path).
- Every git commit message ends with the trailer, on its own line:
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`
- Commit directly to `master` (matches this repo's established workflow).

### Build & test commands (Windows, PowerShell or Bash)

- Configure once: `cmake --preset windows-debug`
- Build the test exe: `cmake --build --preset windows-debug --target bumpy_tests`
- Run one test: `./build/windows-debug/Debug/bumpy_tests.exe "<test case name>"`
- Run the whole suite: `./build/windows-debug/Debug/bumpy_tests.exe`
- Build the game: `cmake --build --preset windows-debug --target bumpy_port`
- Run the game: `./build/windows-debug/Debug/bumpy_port.exe`

## File Structure

| File | Responsibility |
|------|----------------|
| `src/core/port_config.{h,cpp}` | +`music`/`sfx` fields, flipped defaults, parse/serialize |
| `src/audio/audio_engine.{h,cpp}` | `set_sfx_enabled(bool)` + `play_sfx` gate |
| `src/game/settings_overlay.{h,cpp}` | `SettingsPage`/`SettingsEvent`/`SettingsView`/`SettingsOverlay` model (SDL-free, tested) |
| `src/video/settings_renderer.{h,cpp}` | `SettingsRenderer` — draws a `SettingsView` full-screen |
| `src/platform_sdl3/sdl_app.{h,cpp}` | Tab handling, input routing, event application, present, keep hotkeys |
| `src/app/main.cpp` | build `SettingsRenderer`, seed audio, `--render-options` dump tool |
| `tests/cpp/settings_overlay_test.cpp` | model tests |
| `tests/cpp/settings_renderer_test.cpp` | headless render smoke test |
| `tests/cpp/port_config_test.cpp` | updated defaults + new keys |
| `tests/cpp/audio_engine_test.cpp` | SFX-gate test |
| `CMakeLists.txt` | register new sources + tests |

---

## Task 1: PortConfig — new fields, flipped defaults, parse/serialize

**Files:**
- Modify: `src/core/port_config.h`
- Modify: `src/core/port_config.cpp`
- Test: `tests/cpp/port_config_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct bumpy::PortConfig { bool render3d=true; bool square_pixels=false; bool fullscreen=true; bool music=true; bool sfx=true; };` and the existing free functions `parse_port_config`/`serialize_port_config`/`load_port_config`/`save_port_config` now round-tripping `music`/`sfx`.

- [ ] **Step 1: Update the existing default/round-trip tests to the new expectations**

Replace the first two `TEST_CASE`s in `tests/cpp/port_config_test.cpp` (the defaults case and the round-trip case) with:

```cpp
TEST_CASE("defaults: 3D on, 4:3 aspect, fullscreen, audio on") {
    const PortConfig c{};
    REQUIRE(c.render3d);
    REQUIRE_FALSE(c.square_pixels);  // false == 4:3
    REQUIRE(c.fullscreen);
    REQUIRE(c.music);
    REQUIRE(c.sfx);
}

TEST_CASE("serialize/parse round-trips every field") {
    PortConfig c;
    c.render3d = false;
    c.square_pixels = true;
    c.fullscreen = false;
    c.music = false;
    c.sfx = false;
    const auto back = bumpy::parse_port_config(bumpy::serialize_port_config(c));
    REQUIRE_FALSE(back.render3d);
    REQUIRE(back.square_pixels);
    REQUIRE_FALSE(back.fullscreen);
    REQUIRE_FALSE(back.music);
    REQUIRE_FALSE(back.sfx);
}

TEST_CASE("an old file without the audio keys parses them as enabled") {
    const auto c = bumpy::parse_port_config("render3d=0\nsquare_pixels=1\nfullscreen=0\n");
    REQUIRE(c.music);   // key absent -> default true
    REQUIRE(c.sfx);
}
```

Also update the two later cases that assume old defaults:
- In `"parse ignores unknown keys..."`, change `REQUIRE(c.square_pixels);` to `REQUIRE_FALSE(c.square_pixels);` and `REQUIRE_FALSE(c.fullscreen);` stays (fullscreen not in that input, so it keeps the new default) → change it to `REQUIRE(c.fullscreen);`.
- In `"parse tolerates garbage values"`, the input `"render3d=banana\nsquare_pixels=\n=1\n"` leaves both at defaults, so change `REQUIRE_FALSE(c.render3d);` to `REQUIRE(c.render3d);` and `REQUIRE(c.square_pixels);` to `REQUIRE_FALSE(c.square_pixels);`.
- In `"load returns defaults for a missing file..."`, change `REQUIRE_FALSE(missing.render3d);` to `REQUIRE(missing.render3d);`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows-debug --target bumpy_tests`
Expected: FAIL to compile — the tests reference `PortConfig::music`/`sfx`, which do not exist yet. (A build error is the expected "fail" here.)

- [ ] **Step 3: Add the fields and flip the defaults**

In `src/core/port_config.h`, replace the struct body:

```cpp
struct PortConfig {
    bool render3d = true;       // Alt+3 diorama mode (on by default)
    bool square_pixels = false; // Alt+A: true = 16:10, false = 4:3 (CRT, default)
    bool fullscreen = true;     // Alt+Enter (fullscreen by default)
    bool music = true;          // intro-music gate (Tab overlay AUDIO page)
    bool sfx = true;            // SFX gate (Tab overlay AUDIO page)
};
```

- [ ] **Step 4: Parse and serialize the new keys**

In `src/core/port_config.cpp`, extend the key dispatch in `parse_port_config` (after the `fullscreen` branch):

```cpp
        } else if (key == "fullscreen") {
            parse_bool(value, config.fullscreen);
        } else if (key == "music") {
            parse_bool(value, config.music);
        } else if (key == "sfx") {
            parse_bool(value, config.sfx);
        }
```

And extend `serialize_port_config` (after the `fullscreen` line):

```cpp
        << "fullscreen=" << (config.fullscreen ? 1 : 0) << '\n'
        << "music=" << (config.music ? 1 : 0) << '\n'
        << "sfx=" << (config.sfx ? 1 : 0) << '\n';
```

- [ ] **Step 5: Run the config tests to verify they pass**

Run: `cmake --build --preset windows-debug --target bumpy_tests` then
`./build/windows-debug/Debug/bumpy_tests.exe`
Expected: PASS (whole suite, including the four config cases).

- [ ] **Step 6: Commit**

```bash
git add src/core/port_config.h src/core/port_config.cpp tests/cpp/port_config_test.cpp
git commit -m "feat(config): default to fullscreen+4:3+3D; add music/sfx keys"
```

---

## Task 2: AudioEngine — SFX gate

**Files:**
- Modify: `src/audio/audio_engine.h`
- Modify: `src/audio/audio_engine.cpp`
- Test: `tests/cpp/audio_engine_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `void bumpy::AudioEngine::set_sfx_enabled(bool enabled);` — when disabled, `play_sfx` starts no voice.

- [ ] **Step 1: Write the failing test**

Append to `tests/cpp/audio_engine_test.cpp`:

```cpp
TEST_CASE("AudioEngine SFX gate silences play_sfx when disabled") {
    const auto song = bumpy::MidiSong::load("BUMPY.MID");
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    bumpy::AudioEngine engine(song, bank);
    std::vector<float> buf(4096, 0.0f);

    engine.set_sfx_enabled(false);
    engine.play_sfx(1);                        // ignored while disabled
    engine.render(buf.data(), buf.size());
    double off = 0.0; for (float s : buf) off += double(s) * s;
    REQUIRE(off == 0.0);

    engine.set_sfx_enabled(true);
    engine.play_sfx(1);                        // audible again
    engine.render(buf.data(), buf.size());
    double on = 0.0; for (float s : buf) on += double(s) * s;
    REQUIRE(on > 0.0);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset windows-debug --target bumpy_tests`
Expected: FAIL to compile — `set_sfx_enabled` is not declared.

- [ ] **Step 3: Declare the method and flag**

In `src/audio/audio_engine.h`, add the declaration after `void play_sfx(std::uint8_t id);`:

```cpp
    // Enable/disable the SFX bus (Tab overlay AUDIO page). When disabled, play_sfx
    // starts no voice. Music is unaffected (it has its own start/stop).
    void set_sfx_enabled(bool enabled);
```

And add to the private members (near `bool music_playing_`):

```cpp
    bool sfx_enabled_ = true;
```

- [ ] **Step 4: Implement the method and the gate**

In `src/audio/audio_engine.cpp`, add after `stop_music()`:

```cpp
void AudioEngine::set_sfx_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    sfx_enabled_ = enabled;
}
```

And in `play_sfx`, add the gate immediately after acquiring the lock:

```cpp
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sfx_enabled_) {
        return;
    }
    std::size_t slot = 0;
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build --preset windows-debug --target bumpy_tests` then
`./build/windows-debug/Debug/bumpy_tests.exe "AudioEngine SFX gate silences play_sfx when disabled"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/audio/audio_engine.h src/audio/audio_engine.cpp tests/cpp/audio_engine_test.cpp
git commit -m "feat(audio): SFX enable/disable gate for the settings overlay"
```

---

## Task 3: SettingsOverlay — navigation model

**Files:**
- Create: `src/game/settings_overlay.h`
- Create: `src/game/settings_overlay.cpp`
- Test: `tests/cpp/settings_overlay_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `bumpy::MenuInput` (from `game/menu.h`).
- Produces:
  - `enum class bumpy::SettingsPage { root, video, audio, passwords };`
  - `enum class bumpy::SettingsEvent { none, toggle_3d, toggle_aspect, toggle_fullscreen, toggle_music, toggle_sfx, quit, close };`
  - `struct bumpy::SettingsView { SettingsPage page; int cursor_row; bool render3d, square_pixels, fullscreen, music, sfx, render3d_available; };`
  - `class bumpy::SettingsOverlay` with `SettingsEvent update(const MenuInput&, bool render3d_available) noexcept;`, `void reset() noexcept;`, `SettingsPage page() const noexcept;`, `int cursor_row() const noexcept;`.

- [ ] **Step 1: Create the header**

Create `src/game/settings_overlay.h`:

```cpp
#pragma once

#include "game/menu.h"  // MenuInput

namespace bumpy {

enum class SettingsPage { root, video, audio, passwords };

enum class SettingsEvent {
    none,
    toggle_3d,
    toggle_aspect,
    toggle_fullscreen,
    toggle_music,
    toggle_sfx,
    quit,
    close,
};

// Selectable-row counts per page (passwords is read-only). Shared with the renderer.
inline constexpr int kRootRowCount = 4;   // VIDEO, AUDIO, PASSWORDS, QUIT
inline constexpr int kVideoRowCount = 3;  // 3D, ASPECT, FULLSCREEN
inline constexpr int kAudioRowCount = 2;  // MUSIC, SOUND

// Per-frame snapshot the shell assembles from the overlay's nav state plus the live
// PortConfig/GL values, consumed by SettingsRenderer. The overlay stores no values.
struct SettingsView {
    SettingsPage page{SettingsPage::root};
    int cursor_row{};
    bool render3d{};
    bool square_pixels{};
    bool fullscreen{};
    bool music{};
    bool sfx{};
    bool render3d_available{};  // false -> the 3D row cannot be toggled
};

// SDL-independent navigation model for the Tab settings overlay. Owns only the current
// page + cursor (+ a press-debounce like Menu); update() emits events instead of
// mutating settings, so the shell's PortConfig stays the single source of truth.
class SettingsOverlay {
public:
    SettingsEvent update(const MenuInput& input, bool render3d_available) noexcept;
    void reset() noexcept;  // page=root, cursor=0 (called when Tab opens the overlay)

    [[nodiscard]] SettingsPage page() const noexcept { return page_; }
    [[nodiscard]] int cursor_row() const noexcept { return cursor_row_; }

private:
    [[nodiscard]] int row_count() const noexcept;

    SettingsPage page_{SettingsPage::root};
    int cursor_row_{};
    bool waiting_for_release_{};
};

}  // namespace bumpy
```

- [ ] **Step 2: Write the failing tests**

Create `tests/cpp/settings_overlay_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "game/settings_overlay.h"

using bumpy::MenuInput;
using bumpy::SettingsEvent;
using bumpy::SettingsOverlay;
using bumpy::SettingsPage;

namespace {
// One debounced key press: the action frame, then a released frame to clear the latch.
SettingsEvent press(SettingsOverlay& o, const MenuInput& in, bool gl = true) {
    const SettingsEvent e = o.update(in, gl);
    o.update(MenuInput{}, gl);  // release
    return e;
}
}  // namespace

TEST_CASE("overlay starts on the root page at cursor 0") {
    SettingsOverlay o;
    REQUIRE(o.page() == SettingsPage::root);
    REQUIRE(o.cursor_row() == 0);
}

TEST_CASE("root cursor moves and wraps") {
    SettingsOverlay o;
    REQUIRE(press(o, MenuInput{.up = true}) == SettingsEvent::none);
    REQUIRE(o.cursor_row() == bumpy::kRootRowCount - 1);  // wrap up to QUIT
    REQUIRE(press(o, MenuInput{.down = true}) == SettingsEvent::none);
    REQUIRE(o.cursor_row() == 0);                          // wrap back to VIDEO
}

TEST_CASE("root enters each sub-page and backs out to root") {
    SettingsOverlay o;
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::none);  // VIDEO
    REQUIRE(o.page() == SettingsPage::video);
    REQUIRE(press(o, MenuInput{.cancel = true}) == SettingsEvent::none);   // back
    REQUIRE(o.page() == SettingsPage::root);

    REQUIRE(press(o, MenuInput{.down = true}) == SettingsEvent::none);     // -> AUDIO row
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::none);
    REQUIRE(o.page() == SettingsPage::audio);
    REQUIRE(press(o, MenuInput{.left = true}) == SettingsEvent::none);     // left == back
    REQUIRE(o.page() == SettingsPage::root);
}

TEST_CASE("video rows emit the right toggle events") {
    SettingsOverlay o;
    press(o, MenuInput{.confirm = true});                 // enter VIDEO
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_3d);      // row 0
    press(o, MenuInput{.down = true});
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_aspect);  // row 1
    press(o, MenuInput{.down = true});
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_fullscreen); // row 2
}

TEST_CASE("3D toggle is suppressed when GL is unavailable") {
    SettingsOverlay o;
    press(o, MenuInput{.confirm = true});                 // enter VIDEO, row 0 (3D)
    REQUIRE(press(o, MenuInput{.confirm = true}, /*gl=*/false) == SettingsEvent::none);
}

TEST_CASE("audio rows emit the right toggle events") {
    SettingsOverlay o;
    press(o, MenuInput{.down = true});                    // root -> AUDIO row
    press(o, MenuInput{.confirm = true});                 // enter AUDIO
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_music);   // row 0
    press(o, MenuInput{.down = true});
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::toggle_sfx);     // row 1
}

TEST_CASE("QUIT emits quit; back from root closes") {
    SettingsOverlay o;
    // Move to QUIT (row 3) and confirm.
    press(o, MenuInput{.up = true});                      // wrap up to QUIT
    REQUIRE(o.cursor_row() == bumpy::kRootRowCount - 1);
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::quit);

    SettingsOverlay o2;
    REQUIRE(press(o2, MenuInput{.cancel = true}) == SettingsEvent::close);
    REQUIRE(press(o2, MenuInput{.left = true}) == SettingsEvent::close);
}

TEST_CASE("a held key fires once until released") {
    SettingsOverlay o;
    REQUIRE(o.update(MenuInput{.down = true}, true) == SettingsEvent::none);
    REQUIRE(o.cursor_row() == 1);
    REQUIRE(o.update(MenuInput{.down = true}, true) == SettingsEvent::none);  // still held
    REQUIRE(o.cursor_row() == 1);
    REQUIRE(o.update(MenuInput{}, true) == SettingsEvent::none);              // release
    REQUIRE(o.update(MenuInput{.down = true}, true) == SettingsEvent::none);
    REQUIRE(o.cursor_row() == 2);
}

TEST_CASE("passwords page ignores navigation and confirm; backs out to root") {
    SettingsOverlay o;
    press(o, MenuInput{.down = true});                    // root -> AUDIO row
    press(o, MenuInput{.down = true});                    // -> PASSWORDS row (row 2)
    REQUIRE(o.cursor_row() == 2);
    press(o, MenuInput{.confirm = true});                 // enter PASSWORDS
    REQUIRE(o.page() == SettingsPage::passwords);
    REQUIRE(press(o, MenuInput{.down = true}) == SettingsEvent::none);   // no rows to move
    REQUIRE(o.cursor_row() == 0);
    REQUIRE(press(o, MenuInput{.confirm = true}) == SettingsEvent::none);// read-only
    REQUIRE(press(o, MenuInput{.cancel = true}) == SettingsEvent::none); // back
    REQUIRE(o.page() == SettingsPage::root);
}

TEST_CASE("reset returns to root at cursor 0") {
    SettingsOverlay o;
    press(o, MenuInput{.down = true});
    press(o, MenuInput{.confirm = true});
    o.reset();
    REQUIRE(o.page() == SettingsPage::root);
    REQUIRE(o.cursor_row() == 0);
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cmake --build --preset windows-debug --target bumpy_tests`
Expected: FAIL to compile/link — `settings_overlay.h`/`.cpp` not built yet, and the test file is not registered.

- [ ] **Step 4: Implement the model**

Create `src/game/settings_overlay.cpp`:

```cpp
#include "game/settings_overlay.h"

namespace bumpy {

int SettingsOverlay::row_count() const noexcept {
    switch (page_) {
    case SettingsPage::root:
        return kRootRowCount;
    case SettingsPage::video:
        return kVideoRowCount;
    case SettingsPage::audio:
        return kAudioRowCount;
    case SettingsPage::passwords:
        return 0;  // read-only: no selectable rows
    }
    return 0;
}

void SettingsOverlay::reset() noexcept {
    page_ = SettingsPage::root;
    cursor_row_ = 0;
    waiting_for_release_ = false;
}

SettingsEvent SettingsOverlay::update(const MenuInput& input, bool render3d_available) noexcept {
    // Press-debounce: one action per key press (matches Menu). A frame with no
    // navigation key clears the latch.
    const bool any = input.up || input.down || input.left || input.right ||
                     input.confirm || input.cancel;
    if (!any) {
        waiting_for_release_ = false;
        return SettingsEvent::none;
    }
    if (waiting_for_release_) {
        return SettingsEvent::none;
    }
    waiting_for_release_ = true;

    const int rows = row_count();
    if (input.up && rows > 0) {
        cursor_row_ = (cursor_row_ + rows - 1) % rows;
        return SettingsEvent::none;
    }
    if (input.down && rows > 0) {
        cursor_row_ = (cursor_row_ + 1) % rows;
        return SettingsEvent::none;
    }

    // Back-out: cancel (Esc) or left. Sub-page -> root; root -> close.
    if (input.cancel || input.left) {
        if (page_ == SettingsPage::root) {
            return SettingsEvent::close;
        }
        page_ = SettingsPage::root;
        cursor_row_ = 0;
        return SettingsEvent::none;
    }

    // Activate: confirm (Enter/Space) or right.
    if (input.confirm || input.right) {
        switch (page_) {
        case SettingsPage::root:
            switch (cursor_row_) {
            case 0: page_ = SettingsPage::video; cursor_row_ = 0; return SettingsEvent::none;
            case 1: page_ = SettingsPage::audio; cursor_row_ = 0; return SettingsEvent::none;
            case 2: page_ = SettingsPage::passwords; cursor_row_ = 0; return SettingsEvent::none;
            case 3: return SettingsEvent::quit;
            }
            return SettingsEvent::none;
        case SettingsPage::video:
            switch (cursor_row_) {
            case 0: return render3d_available ? SettingsEvent::toggle_3d : SettingsEvent::none;
            case 1: return SettingsEvent::toggle_aspect;
            case 2: return SettingsEvent::toggle_fullscreen;
            }
            return SettingsEvent::none;
        case SettingsPage::audio:
            switch (cursor_row_) {
            case 0: return SettingsEvent::toggle_music;
            case 1: return SettingsEvent::toggle_sfx;
            }
            return SettingsEvent::none;
        case SettingsPage::passwords:
            return SettingsEvent::none;
        }
    }
    return SettingsEvent::none;
}

}  // namespace bumpy
```

- [ ] **Step 5: Register the source and test in CMake**

In `CMakeLists.txt`, add to the `bumpy_core` source list (after `src/game/app.cpp`):

```cmake
  src/game/settings_overlay.cpp
```

And add to the `bumpy_tests` source list (after `tests/cpp/app_test.cpp`):

```cmake
  tests/cpp/settings_overlay_test.cpp
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build --preset windows-debug --target bumpy_tests` then
`./build/windows-debug/Debug/bumpy_tests.exe`
Expected: PASS (whole suite, including all the overlay cases).

- [ ] **Step 7: Commit**

```bash
git add src/game/settings_overlay.h src/game/settings_overlay.cpp tests/cpp/settings_overlay_test.cpp CMakeLists.txt
git commit -m "feat(overlay): SettingsOverlay navigation model + tests"
```

---

## Task 4: SettingsRenderer + `--render-options` dump

**Files:**
- Create: `src/video/settings_renderer.h`
- Create: `src/video/settings_renderer.cpp`
- Test: `tests/cpp/settings_renderer_test.cpp`
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SettingsView`, `SettingsPage` (Task 3); `draw_glyph_string` (`video/high_score_renderer.h`); `decode_sprite_frame`/`MenuImage` (`resources/sprite_frame.h`, `video/menu_renderer.h`); `apply_screen_image_palette`/`is_screen_image` (`video/screen_image.h`); `password_code_for_world` (`game/password_screen.h`).
- Produces: `class bumpy::SettingsRenderer` with a constructor taking `(score_vec, sprite_bank, cursor_sprite)` spans and `void render(const SettingsView&, IndexedFramebuffer&) const;`.

- [ ] **Step 1: Create the header**

Create `src/video/settings_renderer.h`:

```cpp
#pragma once

#include "core/indexed_framebuffer.h"
#include "game/settings_overlay.h"  // SettingsView, SettingsPage

#include <cstdint>
#include <span>

namespace bumpy {

// Draws the Tab settings overlay as an opaque full-screen page on the SCORE.VEC
// palette, in the same big sprite-glyph style as GAME OVER / PASSWORD / HIGH SCORES.
// Constructed once (the asset spans must outlive it) and reused per frame.
class SettingsRenderer {
public:
    // score_vec    : raw SCORE.VEC screen bytes (palette source)
    // sprite_bank  : BUMSPJEU sprite bank (glyph frames)
    // cursor_sprite: FLECHE.BIN bytes (arrow marker, frame 0)
    SettingsRenderer(std::span<const std::uint8_t> score_vec,
                     std::span<const std::uint8_t> sprite_bank,
                     std::span<const std::uint8_t> cursor_sprite);

    void render(const SettingsView& view, IndexedFramebuffer& target) const;

private:
    std::span<const std::uint8_t> score_vec_;
    std::span<const std::uint8_t> sprite_bank_;
    std::span<const std::uint8_t> cursor_sprite_;
};

}  // namespace bumpy
```

- [ ] **Step 2: Write the failing smoke test**

Create `tests/cpp/settings_renderer_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "resources/binary_reader.h"
#include "resources/sprite_frame.h"
#include "video/screen_image.h"
#include "video/settings_renderer.h"

namespace {
int nonzero_pixels(bumpy::IndexedFramebuffer& f, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < f.width(); ++x)
            if (f.pixel(x, y) != 0) ++n;
    return n;
}
}  // namespace

TEST_CASE("SettingsRenderer draws the root page title and rows") {
    const auto score = bumpy::read_binary_file("SCORE.VEC");
    REQUIRE(bumpy::is_screen_image(score));
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto fleche = bumpy::read_binary_file("FLECHE.BIN");
    bumpy::SettingsRenderer renderer(score, bank.bytes(), fleche);

    bumpy::SettingsView view{};
    view.page = bumpy::SettingsPage::root;
    view.render3d_available = true;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);

    REQUIRE(nonzero_pixels(frame, 16, 32) > 20);    // OPTIONS title band
    REQUIRE(nonzero_pixels(frame, 64, 152) > 40);   // the four rows + cursor
    REQUIRE(nonzero_pixels(frame, 0, 16) == 0);     // clear above the title
}

TEST_CASE("SettingsRenderer draws the passwords page") {
    const auto score = bumpy::read_binary_file("SCORE.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto fleche = bumpy::read_binary_file("FLECHE.BIN");
    bumpy::SettingsRenderer renderer(score, bank.bytes(), fleche);

    bumpy::SettingsView view{};
    view.page = bumpy::SettingsPage::passwords;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);

    REQUIRE(nonzero_pixels(frame, 56, 160) > 60);   // 8 code rows
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build --preset windows-debug --target bumpy_tests`
Expected: FAIL to compile — `settings_renderer.h`/`.cpp` not built yet, test not registered.

- [ ] **Step 4: Implement the renderer**

Create `src/video/settings_renderer.cpp`:

```cpp
#include "video/settings_renderer.h"

#include "game/password_screen.h"       // password_code_for_world
#include "resources/sprite_frame.h"     // decode_sprite_frame, sprite_transparent_index
#include "video/high_score_renderer.h"  // draw_glyph_string
#include "video/menu_renderer.h"        // MenuImage
#include "video/screen_image.h"         // is_screen_image, apply_screen_image_palette

#include <array>
#include <cstddef>
#include <exception>
#include <string>  // std::char_traits

namespace bumpy {
namespace {

constexpr int kGlyphStepX = 16;
constexpr int kTitleY = 16;
constexpr int kRowY0 = 64;
constexpr int kRowStepY = 24;
constexpr int kCursorX = 40;
constexpr int kLabelX = 64;
constexpr int kValueX = 208;
// Passwords page (two columns of four).
constexpr int kPwRowY0 = 56;
constexpr int kPwRowStep = 24;
constexpr int kPwLeftX = 8;
constexpr int kPwRightX = 168;

// Blit one decoded sprite frame with its top-left at (x, y); the shared decoder
// normalizes transparency to sprite_transparent_index, which is skipped.
void blit_frame(std::span<const std::uint8_t> bank, int frame_index, int x, int y,
                IndexedFramebuffer& target) {
    try {
        const MenuImage g = decode_sprite_frame(bank, frame_index);
        for (int py = 0; py < g.height; ++py) {
            const int ty = y + py;
            if (ty < 0 || ty >= target.height()) continue;
            for (int px = 0; px < g.width; ++px) {
                const int tx = x + px;
                if (tx < 0 || tx >= target.width()) continue;
                const auto c = g.pixels[static_cast<std::size_t>(py) * g.width + px];
                if (c != sprite_transparent_index) target.pixel(tx, ty) = c;
            }
        }
    } catch (const std::exception&) {
        // an undecodable frame is skipped
    }
}

}  // namespace

SettingsRenderer::SettingsRenderer(std::span<const std::uint8_t> score_vec,
                                   std::span<const std::uint8_t> sprite_bank,
                                   std::span<const std::uint8_t> cursor_sprite)
    : score_vec_(score_vec), sprite_bank_(sprite_bank), cursor_sprite_(cursor_sprite) {}

void SettingsRenderer::render(const SettingsView& view, IndexedFramebuffer& target) const {
    if (is_screen_image(score_vec_)) {
        apply_screen_image_palette(score_vec_, target);
    }
    target.clear(0);

    auto title = [&](const char* t) {
        const auto len = std::char_traits<char>::length(t);
        const int tx = (target.width() - static_cast<int>(len) * kGlyphStepX) / 2;
        draw_glyph_string(t, len, tx, kTitleY, sprite_bank_, target);
    };
    auto row = [&](int i, const char* label, const char* value) {
        const int y = kRowY0 + i * kRowStepY;
        draw_glyph_string(label, std::char_traits<char>::length(label), kLabelX, y,
                          sprite_bank_, target);
        if (value) {
            draw_glyph_string(value, std::char_traits<char>::length(value), kValueX, y,
                              sprite_bank_, target);
        }
    };

    switch (view.page) {
    case SettingsPage::root:
        title("OPTIONS");
        row(0, "VIDEO", nullptr);
        row(1, "AUDIO", nullptr);
        row(2, "PASSWORDS", nullptr);
        row(3, "QUIT", nullptr);
        break;
    case SettingsPage::video:
        title("VIDEO");
        row(0, "3D", view.render3d ? "ON" : "OFF");
        row(1, "ASPECT", view.square_pixels ? "16.10" : "4.3");
        row(2, "FULLSCREEN", view.fullscreen ? "ON" : "OFF");
        break;
    case SettingsPage::audio:
        title("AUDIO");
        row(0, "MUSIC", view.music ? "ON" : "OFF");
        row(1, "SOUND", view.sfx ? "ON" : "OFF");
        break;
    case SettingsPage::passwords:
        title("PASSWORDS");
        for (int i = 0; i < 8; ++i) {
            const int world = 2 + i;
            const std::array<char, 6> code = password_code_for_world(world);
            char buf[8];
            buf[0] = static_cast<char>('0' + world);
            buf[1] = ' ';
            for (int k = 0; k < 6; ++k) buf[2 + k] = code[static_cast<std::size_t>(k)];
            const int x = (i / 4 == 0) ? kPwLeftX : kPwRightX;
            const int y = kPwRowY0 + (i % 4) * kPwRowStep;
            draw_glyph_string(buf, 8, x, y, sprite_bank_, target);
        }
        break;
    }

    if (view.page == SettingsPage::root || view.page == SettingsPage::video ||
        view.page == SettingsPage::audio) {
        blit_frame(cursor_sprite_, 0, kCursorX, kRowY0 + view.cursor_row * kRowStepY, target);
    }
}

}  // namespace bumpy
```

- [ ] **Step 5: Register the source and test in CMake**

In `CMakeLists.txt`, add to `bumpy_core` (after `src/video/password_renderer.cpp`):

```cmake
  src/video/settings_renderer.cpp
```

And to `bumpy_tests` (after `tests/cpp/settings_overlay_test.cpp`):

```cmake
  tests/cpp/settings_renderer_test.cpp
```

- [ ] **Step 6: Run the render tests to verify they pass**

Run: `cmake --build --preset windows-debug --target bumpy_tests` then
`./build/windows-debug/Debug/bumpy_tests.exe "SettingsRenderer draws the root page title and rows" "SettingsRenderer draws the passwords page"`
Expected: PASS.

- [ ] **Step 7: Add the `--render-options` offline dump tool**

In `src/app/main.cpp`, add the include near the other video includes (after `#include "video/screen_image.h"`):

```cpp
#include "video/settings_renderer.h"
```

Add this function next to the other `render_*_to_bmp` helpers (e.g. after `render_password_to_bmp`):

```cpp
// Dump a settings-overlay page through the shared renderer for by-eye checking.
// page = root | video | audio | passwords (default root).
int render_options_to_bmp(const std::filesystem::path& asset_root,
                          const std::filesystem::path& score_path,
                          const std::filesystem::path& out_path, const std::string& page) {
    const auto score_bytes = bumpy::read_binary_file(score_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    const auto fleche = bumpy::read_binary_file(asset_root / "FLECHE.BIN");
    bumpy::SettingsRenderer renderer(score_bytes, bank.bytes(), fleche);

    bumpy::SettingsView view{};
    view.render3d = true;
    view.square_pixels = false;
    view.fullscreen = true;
    view.music = true;
    view.sfx = true;
    view.render3d_available = true;
    view.page = page == "video"       ? bumpy::SettingsPage::video
                : page == "audio"     ? bumpy::SettingsPage::audio
                : page == "passwords" ? bumpy::SettingsPage::passwords
                                      : bumpy::SettingsPage::root;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << " (" << page << ")\n";
    return 0;
}
```

Add the CLI branch inside `main`, next to the other `--render-*` branches (e.g. after the `--render-password` branch):

```cpp
        if ((argc == 4 || argc == 5) && std::string_view(argv[1]) == "--render-options") {
            // --render-options <SCORE.VEC> <out.bmp> [root|video|audio|passwords]
            return render_options_to_bmp(asset_root, argv[2], argv[3], argc == 5 ? argv[4] : "root");
        }
```

- [ ] **Step 8: Build the game and dump all four pages for a by-eye check**

Run:
```bash
cmake --build --preset windows-debug --target bumpy_port
./build/windows-debug/Debug/bumpy_port.exe --render-options SCORE.VEC options_root.bmp root
./build/windows-debug/Debug/bumpy_port.exe --render-options SCORE.VEC options_video.bmp video
./build/windows-debug/Debug/bumpy_port.exe --render-options SCORE.VEC options_audio.bmp audio
./build/windows-debug/Debug/bumpy_port.exe --render-options SCORE.VEC options_pw.bmp passwords
```
Expected: four `wrote ... (page)` lines. Open the BMPs: OPTIONS/VIDEO/AUDIO/PASSWORDS titles centered, rows and values legible, arrow cursor on row 0 for the non-passwords pages, all eight world codes on the passwords page. Delete the BMPs after review (they are throwaway).

- [ ] **Step 9: Commit**

```bash
git add src/video/settings_renderer.h src/video/settings_renderer.cpp tests/cpp/settings_renderer_test.cpp src/app/main.cpp CMakeLists.txt
git commit -m "feat(overlay): SettingsRenderer + --render-options dump"
```

---

## Task 5: Shell wiring — Tab, input routing, event application

**Files:**
- Modify: `src/platform_sdl3/sdl_app.h`
- Modify: `src/platform_sdl3/sdl_app.cpp`
- Modify: `src/app/main.cpp`

**Interfaces:**
- Consumes: `SettingsOverlay`/`SettingsEvent`/`SettingsView` (Task 3), `SettingsRenderer` (Task 4), `AudioEngine::set_sfx_enabled` (Task 2), `PortConfig::music`/`sfx` (Task 1).
- Produces: the running feature (no new public API beyond the extra `run()` parameter).

- [ ] **Step 1: Add the `SettingsRenderer` parameter to `run()` in the header**

In `src/platform_sdl3/sdl_app.h`, add the include after `#include "video/menu_renderer.h"`:

```cpp
#include "video/settings_renderer.h"
```

And add the parameter to `run` (right after `const MenuRenderer& menu_renderer,`):

```cpp
    int run(App& app, const MenuRenderer& menu_renderer,
            const SettingsRenderer& settings_renderer,
            const std::filesystem::path& asset_root,
            WorldResources world, std::span<const std::uint8_t> sprite_bank, const Font& font,
            std::span<const std::uint8_t> splash_screen, std::span<const std::uint8_t> outro_screen,
            std::span<const std::uint8_t> score_screen,
            IndexedFramebuffer& frame, AudioEngine& audio, PortConfig config,
            std::filesystem::path config_path);
```

- [ ] **Step 2: Match the definition signature and add includes in the cpp**

In `src/platform_sdl3/sdl_app.cpp`, add near the other game includes (after `#include "game/level_game.h"`):

```cpp
#include "game/settings_overlay.h"
```
(`video/settings_renderer.h` arrives transitively via `sdl_app.h`.)

Update the definition signature to match the header (add `const MenuRenderer& menu_renderer, const SettingsRenderer& settings_renderer,` — i.e. insert the new parameter in the same position):

```cpp
int SdlApp::run(App& app, const MenuRenderer& menu_renderer,
                const SettingsRenderer& settings_renderer,
                const std::filesystem::path& asset_root,
                WorldResources world, std::span<const std::uint8_t> sprite_bank, const Font& font,
                std::span<const std::uint8_t> splash_screen,
                std::span<const std::uint8_t> outro_screen,
                std::span<const std::uint8_t> score_screen, IndexedFramebuffer& frame,
                AudioEngine& audio, PortConfig config, std::filesystem::path config_path) {
```

- [ ] **Step 3: Declare overlay state and gate the initial music/SFX on config**

In `src/platform_sdl3/sdl_app.cpp`, at the top of `run` where `running`/`input` are declared, add:

```cpp
    bool running = true;
    MenuInput input{};
    SettingsOverlay overlay;
    bool overlay_open = false;
```

Change the initial splash-music arm to respect `config.music`:

```cpp
    if (app.screen() == Screen::splash && config.music) {
        audio.start_music();
    }
```

Immediately after that block, seed the SFX gate from config:

```cpp
    audio.set_sfx_enabled(config.sfx);
```

- [ ] **Step 4: Gate the splash-entry music on config**

Find the screen-change music handling and gate the `start_music()` call:

```cpp
            if (before == Screen::splash) {
                audio.stop_music();
            } else if (app.screen() == Screen::splash && config.music) {
                audio.start_music();
            }
```

- [ ] **Step 5: Handle Tab in the event loop**

In the `SDL_EVENT_KEY_DOWN && !event.key.repeat` handler, add a `SDLK_TAB` branch just before the final `else { update_key_state(input, event.key.key, true); }`:

```cpp
                } else if (event.key.key == SDLK_TAB) {
                    // Tab: open/close the settings overlay. Only openable on the play
                    // surfaces (menu / map / level); a no-op elsewhere.
                    if (overlay_open) {
                        overlay_open = false;
                    } else if (app.screen() == Screen::menu || app.screen() == Screen::map ||
                               app.screen() == Screen::level) {
                        overlay.reset();
                        overlay_open = true;
                    }
                } else {
                    update_key_state(input, event.key.key, true);
                }
```

- [ ] **Step 6: Run the overlay frame before the transition/app-update block**

In `src/platform_sdl3/sdl_app.cpp`, immediately after the `if (!running) { break; }` that follows the event loop, and BEFORE the `if (transition.active())` block, insert:

```cpp
        // Modal settings overlay (Tab). While open, the world is frozen: no app.update,
        // no game tick, no transition stepping. Input drives the overlay; each event is
        // applied here (side effect + PortConfig write), reusing the hotkey side effects.
        if (overlay_open) {
            switch (overlay.update(input, gl_ != nullptr)) {
            case SettingsEvent::toggle_3d:
                if (gl_) {
                    render3d = !render3d;
                    config.render3d = render3d;
                    persist();
                }
                break;
            case SettingsEvent::toggle_aspect:
                square_pixels = !square_pixels;
                apply_aspect();
                config.square_pixels = square_pixels;
                persist();
                break;
            case SettingsEvent::toggle_fullscreen: {
                const bool fs = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                SDL_SetWindowFullscreen(window_, !fs);
                config.fullscreen = !fs;
                persist();
                break;
            }
            case SettingsEvent::toggle_music:
                config.music = !config.music;
                if (config.music) {
                    if (app.screen() == Screen::splash) audio.start_music();
                } else {
                    audio.stop_music();
                }
                persist();
                break;
            case SettingsEvent::toggle_sfx:
                config.sfx = !config.sfx;
                audio.set_sfx_enabled(config.sfx);
                persist();
                break;
            case SettingsEvent::quit:
                running = false;
                break;
            case SettingsEvent::close:
                overlay_open = false;
                break;
            case SettingsEvent::none:
                break;
            }
            if (!running) {
                break;
            }
            if (overlay_open) {
                SettingsView view{};
                view.page = overlay.page();
                view.cursor_row = overlay.cursor_row();
                view.render3d = render3d;
                view.square_pixels = square_pixels;
                view.fullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                view.music = config.music;
                view.sfx = config.sfx;
                view.render3d_available = gl_ != nullptr;
                settings_renderer.render(view, frame);
                present_frame();  // flat path, over both GL and SDL_Renderer back-ends
            }
            wait_next_tick(period_full);  // menu-rate; keeps cadence so close has no burst
            continue;                     // freeze the world under the overlay
        }
```

- [ ] **Step 7: Pass the renderer from `main.cpp`**

In `src/app/main.cpp`, in `run_sdl_menu`, construct the renderer after `score_screen` is loaded and before `sdl.run(...)` (it needs `score_screen`, the sprite bank, and the FLECHE cursor bytes from `resources.cursor_sprites`):

```cpp
    const bumpy::SettingsRenderer settings_renderer(
        score_screen, sprite_bank.bytes(), resources.cursor_sprites);
```

Update the `sdl.run(...)` call to pass it right after `renderer`:

```cpp
    return sdl.run(app, renderer, settings_renderer, asset_root, std::move(world),
                   sprite_bank.bytes(), font, resources.splash.decoded_bytes(),
                   outro.decoded_bytes(), score_screen, frame, audio_engine, config, cfg_path);
```

- [ ] **Step 8: Build the whole project and run the full test suite**

Run:
```bash
cmake --build --preset windows-debug
./build/windows-debug/Debug/bumpy_tests.exe
```
Expected: the project links, and every test passes (`All tests passed`).

- [ ] **Step 9: Manual end-to-end verification (2D and 3D)**

Run `./build/windows-debug/Debug/bumpy_port.exe` and confirm:
- On the menu, **Tab** opens OPTIONS; ↑/↓ move the arrow; Enter enters VIDEO/AUDIO/PASSWORDS; Esc/← backs out; Tab closes.
- **VIDEO**: toggling 3D / ASPECT / FULLSCREEN changes the running game immediately and matches the Alt+3 / Alt+A / Alt+Enter hotkeys (which still work).
- **AUDIO**: toggling MUSIC on the splash screen starts/stops the intro tune; SOUND off silences in-level SFX.
- **PASSWORDS** lists codes 2–9 (ACCESS…SYSTEM).
- **QUIT** exits the game.
- Open the overlay while in a level in **3D mode**: the page shows flat and the ball/monster are frozen; closing resumes the diorama with no speed burst.
- Each change is written to `bumpy_port.cfg` (open it and confirm the `render3d`/`square_pixels`/`fullscreen`/`music`/`sfx` lines update).

- [ ] **Step 10: Commit**

```bash
git add src/platform_sdl3/sdl_app.h src/platform_sdl3/sdl_app.cpp src/app/main.cpp
git commit -m "feat(overlay): Tab settings overlay wired into the SDL shell"
```

---

## Post-implementation notes

- The developer's existing `bumpy_port.cfg` overrides the new launch defaults for its known keys; delete it to observe a clean first-run (fullscreen + 4:3 + 3D). Only `music`/`sfx` get appended on the next write.
- The Alt+3 / Alt+A / Alt+Enter hotkeys are intentionally kept as accelerators.
- Update `PROJECT_STATUS.md` (and the memory note about the display default being 16:10) once merged — the launch default is now 4:3 + 3D + fullscreen. (Documentation-only; can be a follow-up commit.)
```

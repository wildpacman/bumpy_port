# High-Score Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the original's HIGH-SCORE screen — reachable from menu row 1 (view-only) and after Game Over (with the interactive name-entry editor when a qualifying score is achieved) — faithfully, with the baked default table and no disk persistence.

**Architecture:** A pure `HighScoreTable` (7 baked entries, `qualifies`/`insert`) plus a pure `HighScoreScreen` state machine (view/entry modes, held-repeat name editor) — both SDL-free, delegated to by `App` exactly like `WorldMap`. A `high_score_renderer` composes the shared `SCORE.VEC` backdrop with BUMSPJEU sprite glyphs for the table, the caret, and the `GAME OVER` text. `App` gains `Screen::game_over` (timed) and `Screen::high_scores`; the shell renders them and the existing darken-on-screen-change gives the faithful two-darken death flow for free.

**Tech Stack:** C++20, CMake, Catch2 (tests), SDL3 (platform shell only). Existing modules reused: `decode_vec_resource` (`resources/vec.h`), `decode_sprite_archive`/`SpriteArchive::bytes()` (`resources/menu_resources.h`), `decode_sprite_frame` (`resources/sprite_frame.h`), `is_screen_image`/`apply_screen_image_palette`/`draw_screen_image` (`video/screen_image.h`), `IndexedFramebuffer` (`core/indexed_framebuffer.h`).

## Global Constraints

- Game logic (`src/game/*`, `src/video/*`, `src/resources/*`) must not depend on SDL3, the refresh rate, or floating point. SDL3 lives only in `src/platform_sdl3`.
- Never modify the original game files (root-level `*.VEC/*.BIN/*.PAV/*.DEC/*.BUM/*.EXE`); they are read-only inputs.
- C++ tests run with `WORKING_DIRECTORY` = project root, so originals load by bare name (`"SCORE.VEC"`, `"BUMSPJEU.BIN"`).
- Namespace is `bumpy`. Headers use `#pragma once` and `[[nodiscard]]` on pure accessors, matching the existing code.
- Recovered constants are baked with the source offset documented in a comment (the way `src/resources/entity_sprites.cpp` does it). Design + evidence: `docs/superpowers/specs/2026-07-05-high-score-screen-design.md`; recovered addresses in `analysis/generated/decomp/all_functions.c`.
- Build: `cmake --build --preset windows-debug`. Run all tests: `build/windows-debug/Debug/bumpy_tests.exe`. Run one: `build/windows-debug/Debug/bumpy_tests.exe "<test name>"`. Originals verified with `python tools/assets/manifest.py verify`.
- Recovered facts baked here: names/scores are BUMSPJEU sprite glyphs (`'0'-'9'`=frames `0x1ac..0x1b5`, `'A'-'Z'`=`0x1b6..0x1cf`, `'['`=`0x1d0`, `'.'`=blank), glyphs are top-left anchored (origin 0,0); table = 7×8-byte records at `DS:0x8f0`; GAME OVER (`FUN_1000_11eb`) is a timed flash (no keypress); name entry (`FUN_1000_59d3`) is held-repeat, 8 columns, fire commits.

---

### Task 1: `HighScoreTable` — the session table (pure)

**Files:**
- Create: `src/game/high_scores.h`
- Create: `src/game/high_scores.cpp`
- Test: `tests/cpp/high_scores_test.cpp`
- Modify: `CMakeLists.txt` (add `src/game/high_scores.cpp` to `bumpy_core`; add `tests/cpp/high_scores_test.cpp` to `bumpy_tests`)

**Interfaces:**
- Produces:
  - `struct HighScoreEntry { std::array<char, 8> name; std::uint32_t score; };`
  - `inline constexpr std::size_t kHighScoreCount = 7;`
  - `inline constexpr std::size_t kHighScoreNameLength = 8;`
  - `class HighScoreTable` with:
    - `HighScoreTable() noexcept;` (seeds the 7 baked defaults)
    - `[[nodiscard]] const std::array<HighScoreEntry, kHighScoreCount>& entries() const noexcept;`
    - `[[nodiscard]] const HighScoreEntry& entry(std::size_t row) const noexcept;`
    - `[[nodiscard]] HighScoreEntry& entry(std::size_t row) noexcept;` (name editing during entry)
    - `[[nodiscard]] int qualifies(std::uint32_t score) const noexcept;` (insert row 0..6, or -1)
    - `int insert(std::uint32_t score) noexcept;` (shift down, name = 8×'A', return row or -1)

- [ ] **Step 1: Write the failing test**

Create `tests/cpp/high_scores_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "game/high_scores.h"

#include <array>
#include <string>

using bumpy::HighScoreTable;

namespace {
std::string name_of(const HighScoreTable& t, std::size_t row) {
    const auto& n = t.entry(row).name;
    return std::string(n.begin(), n.end());
}
}  // namespace

TEST_CASE("the default table is the 7 baked entries in descending order") {
    const HighScoreTable t;
    REQUIRE(t.entries().size() == 7);
    REQUIRE(name_of(t, 0) == "BIG JIM.");
    REQUIRE(t.entry(0).score == 5000000u);
    REQUIRE(name_of(t, 2) == "STEVE...");
    REQUIRE(t.entry(2).score == 1000000u);
    REQUIRE(name_of(t, 6) == "MIKE....");
    REQUIRE(t.entry(6).score == 500u);
}

TEST_CASE("qualifies finds the first row the score beats, strictly") {
    const HighScoreTable t;
    REQUIRE(t.qualifies(499) == -1);        // below the last entry (500)
    REQUIRE(t.qualifies(500) == -1);        // ties do not insert (strict >)
    REQUIRE(t.qualifies(501) == 6);         // beats MIKE only
    REQUIRE(t.qualifies(30001) == 4);       // beats JOHNNY (30000)
    REQUIRE(t.qualifies(6000000) == 0);     // beats the top
}

TEST_CASE("insert shifts lower rows down, seeds AAAAAAAA, drops the last") {
    HighScoreTable t;
    const int row = t.insert(2000000);      // beats STEVE (row 2)
    REQUIRE(row == 2);
    REQUIRE(name_of(t, 2) == "AAAAAAAA");
    REQUIRE(t.entry(2).score == 2000000u);
    REQUIRE(name_of(t, 3) == "STEVE...");   // STEVE pushed to row 3
    REQUIRE(t.entry(3).score == 1000000u);
    REQUIRE(name_of(t, 6) == "FRANK...");   // old row 6 (MIKE) dropped
    REQUIRE(t.entry(6).score == 4000u);
}

TEST_CASE("insert at the last row replaces it and shifts nothing") {
    HighScoreTable t;
    const int row = t.insert(1000);         // beats MIKE (500) only
    REQUIRE(row == 6);
    REQUIRE(name_of(t, 6) == "AAAAAAAA");
    REQUIRE(t.entry(6).score == 1000u);
    REQUIRE(name_of(t, 5) == "FRANK...");   // untouched
}

TEST_CASE("insert of a non-qualifying score is a no-op returning -1") {
    HighScoreTable t;
    REQUIRE(t.insert(100) == -1);
    REQUIRE(name_of(t, 6) == "MIKE....");
    REQUIRE(t.entry(6).score == 500u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build/windows-debug/Debug/bumpy_tests.exe "the default table is the 7 baked entries in descending order"`
Expected: build fails — `game/high_scores.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/game/high_scores.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace bumpy {

// One high-score record. In the original these are 7 x 8-byte records at DS:0x8f0
// (name pointer + 32-bit score); the port keeps the displayed name inline. Name
// chars are the glyph alphabet {'.', '0'-'9', 'A'-'Z'} ('.' = blank/pad).
struct HighScoreEntry {
    std::array<char, 8> name;
    std::uint32_t score;
};

inline constexpr std::size_t kHighScoreCount = 7;
inline constexpr std::size_t kHighScoreNameLength = 8;

// The high-score table for one session. Seeded with the 7 baked defaults recovered
// from BUMPY.UNPACKED.EXE (DS:0x8f0, names at DS:0x11e6+); no disk persistence, so it
// resets to the defaults every launch, exactly like the original's data-segment table.
// FUN_1000_57e1 (draw + insert test) / FUN_1000_59d3 (name entry).
class HighScoreTable {
public:
    HighScoreTable() noexcept;

    [[nodiscard]] const std::array<HighScoreEntry, kHighScoreCount>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] const HighScoreEntry& entry(std::size_t row) const noexcept { return entries_[row]; }
    [[nodiscard]] HighScoreEntry& entry(std::size_t row) noexcept { return entries_[row]; }

    // The first row whose score `score` strictly beats, or -1 if it beats none.
    [[nodiscard]] int qualifies(std::uint32_t score) const noexcept;

    // Insert `score`: shift the rows below down one, drop the last, seed the new row's
    // name to 8x'A' (FUN_1000_57e1), and return the inserted row -- or -1 if it did not
    // qualify (no change).
    int insert(std::uint32_t score) noexcept;

private:
    std::array<HighScoreEntry, kHighScoreCount> entries_;
};

}  // namespace bumpy
```

- [ ] **Step 4: Write the implementation**

Create `src/game/high_scores.cpp`:

```cpp
#include "game/high_scores.h"

namespace bumpy {

namespace {
// One 8-char name, padded from a literal (defaults are <= 8 chars).
constexpr std::array<char, 8> name8(const char (&s)[9]) {
    return {s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]};
}
}  // namespace

HighScoreTable::HighScoreTable() noexcept
    // Baked defaults, verified from BUMPY.UNPACKED.EXE (records at DS:0x8f0, file 0x11D30;
    // names in the DS string pool at 0x11e6+). Descending by score.
    : entries_{{
          {name8("BIG JIM."), 5000000},
          {name8("SUPER JO"), 3000000},
          {name8("STEVE..."), 1000000},
          {name8("WILIAM.."), 200000},
          {name8("JOHNNY.."), 30000},
          {name8("FRANK..."), 4000},
          {name8("MIKE...."), 500},
      }} {}

int HighScoreTable::qualifies(std::uint32_t score) const noexcept {
    for (std::size_t row = 0; row < kHighScoreCount; ++row) {
        if (score > entries_[row].score) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

int HighScoreTable::insert(std::uint32_t score) noexcept {
    const int row = qualifies(score);
    if (row < 0) {
        return -1;
    }
    for (int r = static_cast<int>(kHighScoreCount) - 1; r > row; --r) {
        entries_[static_cast<std::size_t>(r)] = entries_[static_cast<std::size_t>(r - 1)];
    }
    entries_[static_cast<std::size_t>(row)].name.fill('A');
    entries_[static_cast<std::size_t>(row)].score = score;
    return row;
}

}  // namespace bumpy
```

Add to `CMakeLists.txt`: `src/game/high_scores.cpp` in the `bumpy_core` source list (after `src/game/menu.cpp`), and `tests/cpp/high_scores_test.cpp` in the `bumpy_tests` list (after `tests/cpp/menu_test.cpp`).

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe "[high" ` is not a tag — instead run the five by name, e.g. `build/windows-debug/Debug/bumpy_tests.exe "insert shifts lower rows down, seeds AAAAAAAA, drops the last"`.
Expected: PASS (all 5). Then run the full suite `build/windows-debug/Debug/bumpy_tests.exe` — Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/game/high_scores.h src/game/high_scores.cpp tests/cpp/high_scores_test.cpp CMakeLists.txt
git commit -m "feat: high-score table model (baked defaults, qualifies/insert)"
```

---

### Task 2: `HighScoreScreen` — view/entry state machine (pure)

**Files:**
- Create: `src/game/high_score_screen.h`
- Create: `src/game/high_score_screen.cpp`
- Test: `tests/cpp/high_score_screen_test.cpp`
- Modify: `CMakeLists.txt` (add both to `bumpy_core` / `bumpy_tests`)

**Interfaces:**
- Consumes: `bumpy::MenuInput` (`game/menu.h`); `bumpy::HighScoreTable` (`game/high_scores.h`).
- Produces:
  - `enum class HighScoreMode { view, entry };`
  - `enum class HighScoreResult { none, done };`
  - `struct HighScoreScreenView { HighScoreMode mode{HighScoreMode::view}; int insert_row{-1}; int cursor_col{0}; bool caret_visible{true}; };`
  - `class HighScoreScreen` with:
    - `void enter_view() noexcept;`
    - `void enter_entry(HighScoreTable& table, std::uint32_t score) noexcept;`
    - `HighScoreResult update(const MenuInput& input) noexcept;`
    - `[[nodiscard]] const HighScoreScreenView& view() const noexcept;`
  - `extern const char* const kNameCycle; inline constexpr int kNameCycleLen = 36;` (or expose the cycle another way — see impl)

- [ ] **Step 1: Write the failing test**

Create `tests/cpp/high_score_screen_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "game/high_score_screen.h"
#include "game/high_scores.h"

using bumpy::HighScoreMode;
using bumpy::HighScoreResult;
using bumpy::HighScoreScreen;
using bumpy::HighScoreTable;
using bumpy::MenuInput;

TEST_CASE("view mode dismisses on any key, but only after the entry key releases") {
    HighScoreScreen s;
    s.enter_view();
    REQUIRE(s.view().mode == HighScoreMode::view);
    REQUIRE(s.view().insert_row == -1);

    // Confirm still held from the menu: must not dismiss until released.
    REQUIRE(s.update(MenuInput{.confirm = true}) == HighScoreResult::none);
    REQUIRE(s.update(MenuInput{}) == HighScoreResult::none);            // release clears the guard
    REQUIRE(s.update(MenuInput{.confirm = true}) == HighScoreResult::done);  // fresh press dismisses
}

TEST_CASE("entry mode with a qualifying score opens the editor at column 0") {
    HighScoreTable table;
    HighScoreScreen s;
    s.enter_entry(table, 2000000);  // beats STEVE -> inserted at row 2, name AAAAAAAA
    REQUIRE(s.view().mode == HighScoreMode::entry);
    REQUIRE(s.view().insert_row == 2);
    REQUIRE(s.view().cursor_col == 0);
    REQUIRE(table.entry(2).name[0] == 'A');
}

TEST_CASE("up/down cycle the caret glyph; left/right move the caret 0..7") {
    HighScoreTable table;
    HighScoreScreen s;
    s.enter_entry(table, 2000000);
    REQUIRE(s.update(MenuInput{}) == HighScoreResult::none);  // release guard

    // Up once: 'A' -> 'B' at column 0.
    REQUIRE(s.update(MenuInput{.up = true}) == HighScoreResult::none);
    REQUIRE(table.entry(2).name[0] == 'B');

    // Right to column 1 (release between actions so the repeat delay does not swallow it).
    for (int i = 0; i < 20; ++i) s.update(MenuInput{});  // let the repeat delay expire
    REQUIRE(s.update(MenuInput{.right = true}) == HighScoreResult::none);
    REQUIRE(s.view().cursor_col == 1);

    // Left cannot go below 0.
    for (int i = 0; i < 20; ++i) s.update(MenuInput{});
    s.update(MenuInput{.left = true});
    for (int i = 0; i < 20; ++i) s.update(MenuInput{});
    s.update(MenuInput{.left = true});
    REQUIRE(s.view().cursor_col == 0);
}

TEST_CASE("fire confirms and finishes the editor") {
    HighScoreTable table;
    HighScoreScreen s;
    s.enter_entry(table, 2000000);
    REQUIRE(s.update(MenuInput{}) == HighScoreResult::none);  // release guard
    REQUIRE(s.update(MenuInput{.confirm = true}) == HighScoreResult::done);
}

TEST_CASE("a game over that does not qualify shows the table and dismisses on any key") {
    HighScoreTable table;
    HighScoreScreen s;
    s.enter_entry(table, 100);  // below MIKE (500): no insert
    REQUIRE(s.view().mode == HighScoreMode::entry);
    REQUIRE(s.view().insert_row == -1);
    REQUIRE(s.update(MenuInput{}) == HighScoreResult::none);            // release guard
    REQUIRE(s.update(MenuInput{.confirm = true}) == HighScoreResult::done);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build/windows-debug/Debug/bumpy_tests.exe "fire confirms and finishes the editor"`
Expected: build fails — `game/high_score_screen.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/game/high_score_screen.h`:

```cpp
#pragma once

#include "game/menu.h"  // MenuInput

namespace bumpy {

class HighScoreTable;

enum class HighScoreMode { view, entry };
enum class HighScoreResult { none, done };

struct HighScoreScreenView {
    HighScoreMode mode{HighScoreMode::view};
    int insert_row{-1};        // caret row (0..6) in entry mode, else -1 (view / no qualify)
    int cursor_col{0};         // caret column 0..7
    bool caret_visible{true};  // blink state for the caret cell
};

// The transient HIGH-SCORE screen state, delegated to by App like WorldMap. Recovered
// from FUN_1000_57e1 (view / any-key dismiss via 328f) and FUN_1000_59d3 (held-repeat
// name editor: up/down cycle the glyph, left/right move the caret across all 8 columns,
// fire commits). No debounce on the editor (the original re-polls the held key each
// iteration) -- held keys repeat, gated by a per-action frame delay.
class HighScoreScreen {
public:
    // From the menu: read-only table, any key returns to the menu (score is 0 -> nothing
    // qualifies in the original).
    void enter_view() noexcept;

    // From Game Over: insert `score` into `table` if it qualifies (driving the caret over
    // the inserted row) or leave it read-only; either way, mode = entry.
    void enter_entry(HighScoreTable& table, std::uint32_t score) noexcept;

    // Advance one frame. Edits the inserted row's name in place during entry mode.
    HighScoreResult update(const MenuInput& input) noexcept;

    [[nodiscard]] const HighScoreScreenView& view() const noexcept { return view_; }

private:
    void cycle_glyph(int direction) noexcept;

    HighScoreScreenView view_{};
    HighScoreTable* table_{};       // the table being edited (entry mode); null in view mode
    bool waiting_for_release_{true};  // swallow the key that opened the screen
    int repeat_{0};                 // frames until the next held-repeat action
    int blink_{0};                  // caret blink counter
};

}  // namespace bumpy
```

- [ ] **Step 4: Write the implementation**

Create `src/game/high_score_screen.cpp`:

```cpp
#include "game/high_score_screen.h"

#include "game/high_scores.h"

#include <cstring>

namespace bumpy {
namespace {

// The name-entry glyph cycle. Recovered from FUN_1000_59d3, which steps the glyph frame
// through [0x1ad, 0x1cf] (chars '1'-'9','A'-'Z') with the 0x1d0 -> 0x1a3 ('.') wrap; the
// port models it as a wrapping char list with '.' (blank) between 'Z' and '1'.
constexpr char kNameCycle[] = ".123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr int kNameCycleLen = 36;  // strlen(kNameCycle)

// Pacing (tuned by eye; the original clocks name entry by buffer commits, not retraces).
constexpr int kNameRepeatFrames = 7;   // held-repeat cadence for the editor
constexpr int kCaretBlinkFrames = 15;  // caret blink half-period

int cycle_index(char c) noexcept {
    for (int i = 0; i < kNameCycleLen; ++i) {
        if (kNameCycle[i] == c) {
            return i;
        }
    }
    return 10;  // 'A' -- the seeded default
}

bool any_key(const MenuInput& in) noexcept {
    return in.up || in.down || in.left || in.right || in.confirm || in.cancel;
}

}  // namespace

void HighScoreScreen::enter_view() noexcept {
    view_ = {};
    view_.mode = HighScoreMode::view;
    view_.insert_row = -1;
    table_ = nullptr;
    waiting_for_release_ = true;
    repeat_ = 0;
    blink_ = 0;
}

void HighScoreScreen::enter_entry(HighScoreTable& table, std::uint32_t score) noexcept {
    view_ = {};
    view_.mode = HighScoreMode::entry;
    view_.insert_row = table.insert(score);  // -1 if it did not qualify
    view_.cursor_col = 0;
    view_.caret_visible = true;
    table_ = &table;
    waiting_for_release_ = true;
    repeat_ = 0;
    blink_ = 0;
}

void HighScoreScreen::cycle_glyph(int direction) noexcept {
    if (table_ == nullptr || view_.insert_row < 0) {
        return;
    }
    char& c = table_->entry(static_cast<std::size_t>(view_.insert_row))
                  .name[static_cast<std::size_t>(view_.cursor_col)];
    int idx = cycle_index(c);
    idx = (idx + direction + kNameCycleLen) % kNameCycleLen;
    c = kNameCycle[idx];
}

HighScoreResult HighScoreScreen::update(const MenuInput& input) noexcept {
    // Release guard: the key that opened the screen (menu confirm, or the death) must be
    // released before any dismissal / edit is accepted (mirrors 328f's fresh-press wait and
    // avoids 59d3's held-fire instant-commit quirk).
    if (waiting_for_release_) {
        if (!any_key(input)) {
            waiting_for_release_ = false;
        }
        return HighScoreResult::none;
    }

    // Blink the caret cell every kCaretBlinkFrames frames.
    if (++blink_ >= kCaretBlinkFrames) {
        blink_ = 0;
        view_.caret_visible = !view_.caret_visible;
    }

    // View mode, or a game over that did not qualify: any key dismisses (FUN_1000_328f).
    if (view_.insert_row < 0) {
        return any_key(input) ? HighScoreResult::done : HighScoreResult::none;
    }

    // Entry mode with a caret: held-repeat editing; fire commits (FUN_1000_59d3). Cancel is
    // ignored -- the original leaves 59d3 only on fire.
    if (input.confirm) {
        return HighScoreResult::done;
    }
    if (repeat_ > 0) {
        --repeat_;
        return HighScoreResult::none;
    }
    bool acted = false;
    if (input.up) {
        cycle_glyph(+1);
        acted = true;
    } else if (input.down) {
        cycle_glyph(-1);
        acted = true;
    } else if (input.left) {
        if (view_.cursor_col > 0) {
            --view_.cursor_col;
            acted = true;
        }
    } else if (input.right) {
        if (view_.cursor_col < 7) {
            ++view_.cursor_col;
            acted = true;
        }
    }
    if (acted) {
        repeat_ = kNameRepeatFrames;
        view_.caret_visible = true;
        blink_ = 0;
    }
    return HighScoreResult::none;
}

}  // namespace bumpy
```

Add `src/game/high_score_screen.cpp` to `bumpy_core` and `tests/cpp/high_score_screen_test.cpp` to `bumpy_tests` in `CMakeLists.txt`.

**Implementer note:** confirm the exact `kNameCycle` order/wrap and the 8-column bound against `FUN_1000_59d3` in `all_functions.c` (the spec cites `[0x1ad,0x1cf]` + `0x1d0->0x1a3` wrap, `col <= 6` blocks a further right so cols 0..7 are editable). Adjust the cycle string only if the disassembly disagrees.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe "up/down cycle the caret glyph; left/right move the caret 0..7"`
Expected: PASS. Then the full suite — Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/game/high_score_screen.h src/game/high_score_screen.cpp tests/cpp/high_score_screen_test.cpp CMakeLists.txt
git commit -m "feat: high-score screen state machine (view + name-entry editor)"
```

---

### Task 3: `high_score_renderer` — SCORE.VEC backdrop + glyph text

**Files:**
- Create: `src/video/high_score_renderer.h`
- Create: `src/video/high_score_renderer.cpp`
- Test: `tests/cpp/high_score_renderer_test.cpp`
- Modify: `CMakeLists.txt` (add both to `bumpy_core` / `bumpy_tests`)

**Interfaces:**
- Consumes: `HighScoreTable` (`game/high_scores.h`), `HighScoreScreenView` (`game/high_score_screen.h`), `IndexedFramebuffer` (`core/indexed_framebuffer.h`), `decode_sprite_frame` (`resources/sprite_frame.h`), `is_screen_image`/`apply_screen_image_palette`/`draw_screen_image` (`video/screen_image.h`).
- Produces:
  - `[[nodiscard]] int high_score_glyph_frame(char c) noexcept;` (BUMSPJEU frame index, or -1 for blank)
  - `void render_game_over(std::span<const std::uint8_t> score_vec, std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);`
  - `void render_high_scores(std::span<const std::uint8_t> score_vec, const HighScoreTable& table, std::span<const std::uint8_t> sprite_bank, const HighScoreScreenView& view, IndexedFramebuffer& target);`

- [ ] **Step 1: Write the failing test**

Create `tests/cpp/high_score_renderer_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "game/high_score_screen.h"
#include "game/high_scores.h"
#include "resources/menu_resources.h"  // decode_sprite_archive
#include "resources/vec.h"             // decode_vec_resource
#include "video/high_score_renderer.h"

namespace {
int nonzero_pixels(const bumpy::IndexedFramebuffer& f, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < f.width(); ++x)
            if (f.pixel(x, y) != 0) ++n;
    return n;
}
}  // namespace

TEST_CASE("glyph frames map digits, letters and the caret") {
    REQUIRE(bumpy::high_score_glyph_frame('0') == 0x1ac);
    REQUIRE(bumpy::high_score_glyph_frame('9') == 0x1b5);
    REQUIRE(bumpy::high_score_glyph_frame('A') == 0x1b6);
    REQUIRE(bumpy::high_score_glyph_frame('Z') == 0x1cf);
    REQUIRE(bumpy::high_score_glyph_frame('[') == 0x1d0);
    REQUIRE(bumpy::high_score_glyph_frame('.') == -1);  // blank
    REQUIRE(bumpy::high_score_glyph_frame(' ') == -1);  // blank
}

TEST_CASE("render_high_scores draws the backdrop and glyph rows") {
    const auto score_vec = bumpy::decode_vec_resource("SCORE.VEC");
    REQUIRE(bumpy::is_screen_image(score_vec.decoded_bytes()));
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");

    bumpy::HighScoreTable table;
    bumpy::HighScoreScreenView view{};  // view mode, no caret
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_high_scores(score_vec.decoded_bytes(), table, bank.bytes(), view, frame);

    // The first name row sits at y = 65 (0x41); glyphs are 14-16 px tall. Expect drawn
    // pixels in the name band.
    REQUIRE(nonzero_pixels(frame, 65, 81) > 50);
}

TEST_CASE("render_game_over draws the GAME OVER band at y=96") {
    const auto score_vec = bumpy::decode_vec_resource("SCORE.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_game_over(score_vec.decoded_bytes(), bank.bytes(), frame);
    REQUIRE(nonzero_pixels(frame, 96, 112) > 30);  // the text band
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build/windows-debug/Debug/bumpy_tests.exe "glyph frames map digits, letters and the caret"`
Expected: build fails — `video/high_score_renderer.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/video/high_score_renderer.h`:

```cpp
#pragma once

#include "core/indexed_framebuffer.h"
#include "game/high_score_screen.h"  // HighScoreScreenView
#include "game/high_scores.h"

#include <cstdint>
#include <span>

namespace bumpy {

// Map a name/score character to its BUMSPJEU sprite-glyph frame, or -1 for a blank cell
// ('.' / space). Recovered sheet: '0'-'9' = 0x1ac..0x1b5, 'A'-'Z' = 0x1b6..0x1cf,
// '[' (caret) = 0x1d0. (FUN_1000_57e1 / FUN_1000_942a(0x792e), glyphs origin (0,0).)
[[nodiscard]] int high_score_glyph_frame(char c) noexcept;

// The GAME OVER screen (FUN_1000_11eb): SCORE.VEC backdrop + "GAME OVER" glyph text at
// column 6, y = 96. `score_vec` is the decoded 320x200 screen-format SCORE.VEC.
void render_game_over(std::span<const std::uint8_t> score_vec,
                      std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target);

// The HIGH-SCORE table (FUN_1000_5681 -> 57e1): SCORE.VEC backdrop + 7 rows of name glyphs
// (x = col*16, y = row*16 + 65) and 7-digit scores (x = 176 + i*16). On view.insert_row the
// caret column draws '[' while view.caret_visible.
void render_high_scores(std::span<const std::uint8_t> score_vec, const HighScoreTable& table,
                        std::span<const std::uint8_t> sprite_bank,
                        const HighScoreScreenView& view, IndexedFramebuffer& target);

}  // namespace bumpy
```

- [ ] **Step 4: Write the implementation**

Create `src/video/high_score_renderer.cpp`:

```cpp
#include "video/high_score_renderer.h"

#include "resources/sprite_frame.h"  // decode_sprite_frame, sprite_transparent_index
#include "video/menu_renderer.h"     // MenuImage
#include "video/screen_image.h"

#include <exception>

namespace bumpy {
namespace {

// Layout, recovered from FUN_1000_57e1 / FUN_1000_11eb.
constexpr int kRowStepY = 16;
constexpr int kNameX0 = 0;
constexpr int kNameRowY0 = 0x41;   // 65
constexpr int kScoreX0 = 0xb0;     // 176
constexpr int kGlyphStepX = 16;
constexpr int kScoreDigits = 7;
constexpr int kGameOverX0 = 6 * 16;  // column 6
constexpr int kGameOverY = 0x60;     // 96
constexpr char kCaretGlyph = '[';

// Blit one decoded glyph frame with its top-left at (x, y) -- glyph frames have origin
// (0,0), so no hotspot offset. Colour index 0 (sprite_transparent_index) is skipped.
void blit_glyph(int frame_index, int x, int y, std::span<const std::uint8_t> bank,
                IndexedFramebuffer& target) {
    try {
        const MenuImage g = decode_sprite_frame(bank, frame_index);
        for (int py = 0; py < g.height; ++py) {
            const int ty = y + py;
            if (ty < 0 || ty >= target.height()) continue;
            for (int px = 0; px < g.width; ++px) {
                const int tx = x + px;
                if (tx < 0 || tx >= target.width()) continue;
                const auto color = g.pixels[static_cast<std::size_t>(py) * g.width + px];
                if (color != sprite_transparent_index) target.pixel(tx, ty) = color;
            }
        }
    } catch (const std::exception&) {
        // an undecodable glyph frame is skipped (blank cell)
    }
}

// Draw a string of glyph cells left-to-right, stepping kGlyphStepX; blank chars skipped.
void draw_glyph_string(const char* text, std::size_t len, int x, int y,
                       std::span<const std::uint8_t> bank, IndexedFramebuffer& target) {
    for (std::size_t i = 0; i < len; ++i) {
        const int frame = high_score_glyph_frame(text[i]);
        if (frame >= 0) blit_glyph(frame, x, y, bank, target);
        x += kGlyphStepX;
    }
}

void draw_background(std::span<const std::uint8_t> score_vec, IndexedFramebuffer& target) {
    if (is_screen_image(score_vec)) {
        apply_screen_image_palette(score_vec, target);
        draw_screen_image(score_vec, target);
    }
}

}  // namespace

int high_score_glyph_frame(char c) noexcept {
    if (c >= '0' && c <= '9') return 0x1ac + (c - '0');
    if (c >= 'A' && c <= 'Z') return 0x1b6 + (c - 'A');
    if (c == kCaretGlyph) return 0x1d0;
    return -1;  // '.', ' ', anything else -> blank
}

void render_game_over(std::span<const std::uint8_t> score_vec,
                      std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target) {
    draw_background(score_vec, target);
    static constexpr char kText[] = "GAME OVER";  // DS:0x1327, 9 chars
    draw_glyph_string(kText, 9, kGameOverX0, kGameOverY, sprite_bank, target);
}

void render_high_scores(std::span<const std::uint8_t> score_vec, const HighScoreTable& table,
                        std::span<const std::uint8_t> sprite_bank,
                        const HighScoreScreenView& view, IndexedFramebuffer& target) {
    draw_background(score_vec, target);

    for (std::size_t row = 0; row < kHighScoreCount; ++row) {
        const int y = static_cast<int>(row) * kRowStepY + kNameRowY0;
        const auto& entry = table.entry(row);

        // Name cells. On the inserted row, the caret column shows '[' while it is visible.
        for (int col = 0; col < static_cast<int>(kHighScoreNameLength); ++col) {
            char c = entry.name[static_cast<std::size_t>(col)];
            if (static_cast<int>(row) == view.insert_row && col == view.cursor_col &&
                view.caret_visible) {
                c = kCaretGlyph;
            }
            const int frame = high_score_glyph_frame(c);
            if (frame >= 0) blit_glyph(frame, kNameX0 + col * kGlyphStepX, y, sprite_bank, target);
        }

        // Score: 7 zero-padded digits.
        char digits[kScoreDigits];
        std::uint32_t value = entry.score;
        for (int i = kScoreDigits - 1; i >= 0; --i) {
            digits[i] = static_cast<char>('0' + value % 10);
            value /= 10;
        }
        draw_glyph_string(digits, kScoreDigits, kScoreX0, y, sprite_bank, target);
    }
}

}  // namespace bumpy
```

Add `src/video/high_score_renderer.cpp` to `bumpy_core` (after `src/video/hud.cpp`) and `tests/cpp/high_score_renderer_test.cpp` to `bumpy_tests`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe "render_high_scores draws the backdrop and glyph rows"`
Expected: PASS. Full suite — Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/video/high_score_renderer.h src/video/high_score_renderer.cpp tests/cpp/high_score_renderer_test.cpp CMakeLists.txt
git commit -m "feat: high-score + GAME OVER renderer (SCORE.VEC + BUMSPJEU glyphs)"
```

---

### Task 4: Menu row 1 → `MenuAction::high_scores`

**Files:**
- Modify: `src/game/menu.h` (add the enum value)
- Modify: `src/game/menu.cpp:47-58` (wire row 1)
- Test: `tests/cpp/menu_test.cpp` (add a case)

**Interfaces:**
- Produces: `MenuAction::high_scores` (a new enumerator alongside `none`, `start_first_level`, `quit`).

- [ ] **Step 1: Write the failing test**

Add to `tests/cpp/menu_test.cpp`:

```cpp
TEST_CASE("menu confirms high scores from the second selection") {
    bumpy::Menu menu;

    REQUIRE(menu.update(bumpy::MenuInput{.down = true}) == bumpy::MenuAction::none);  // row 1
    REQUIRE(menu.update(bumpy::MenuInput{}) == bumpy::MenuAction::none);              // release
    REQUIRE(menu.view().cursor_row == 1);
    REQUIRE(menu.update(bumpy::MenuInput{.confirm = true}) == bumpy::MenuAction::high_scores);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build/windows-debug/Debug/bumpy_tests.exe "menu confirms high scores from the second selection"`
Expected: build fails — `MenuAction::high_scores` is not a member.

- [ ] **Step 3: Add the enum value and wire the row**

In `src/game/menu.h`, extend the enum:

```cpp
enum class MenuAction {
    none,
    start_first_level,
    high_scores,  // row 1 (FUN_1000_5681)
    quit,
};
```

In `src/game/menu.cpp`, inside the `if (input.confirm)` block (currently handling rows 0/2/3), add the row-1 case before the `row == 2` case:

```cpp
    if (input.confirm) {
        if (view_.cursor_row == 0) {
            return MenuAction::start_first_level;
        }
        if (view_.cursor_row == 1) {
            return MenuAction::high_scores;  // FUN_1000_5681
        }
        if (view_.cursor_row == 2) {
            cycle_value_ = static_cast<std::uint8_t>((cycle_value_ + 1U) % 3U);
            return MenuAction::none;
        }
        if (view_.cursor_row == 3) {
            return MenuAction::quit;
        }
    }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe "menu confirms high scores from the second selection"`
Expected: PASS. Full suite — Expected: all pass (the App still only handles `start_first_level`/`quit`; `high_scores` is added next task — a `switch` on `MenuAction` in `app.cpp` may now warn/err on the unhandled case, which Task 5 resolves. If the build breaks here on `app.cpp`'s switch, add a temporary `case MenuAction::high_scores: break;` — Task 5 replaces it.)

- [ ] **Step 5: Commit**

```bash
git add src/game/menu.h src/game/menu.cpp tests/cpp/menu_test.cpp
git commit -m "feat: menu row 1 emits high_scores action"
```

---

### Task 5: `App` integration — game_over + high_scores screens

**Files:**
- Modify: `src/game/app.h` (screens, members, accessors, constant)
- Modify: `src/game/app.cpp` (menu action, new branches, finish_level reroute)
- Test: `tests/cpp/app_test.cpp` (new cases + update the two existing game-over cases)

**Interfaces:**
- Consumes: `HighScoreTable` (`game/high_scores.h`), `HighScoreScreen`/`HighScoreResult`/`HighScoreMode` (`game/high_score_screen.h`), `MenuAction::high_scores`.
- Produces: `Screen::game_over`, `Screen::high_scores`; `App::high_scores()` → `const HighScoreTable&`; `App::high_score_screen()` → `const HighScoreScreen&`.

- [ ] **Step 1: Write the failing tests (and update the two existing game-over cases)**

In `tests/cpp/app_test.cpp`, add a helper below the existing ones:

```cpp
namespace {
// Tick the App with no input until it leaves the timed GAME OVER screen.
void pass_game_over(bumpy::App& app) {
    int guard = 0;
    while (app.screen() == bumpy::Screen::game_over && guard++ < 1000) {
        REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    }
}
}  // namespace
```

Add these cases:

```cpp
TEST_CASE("menu row 1 opens the high-score table in view mode and returns to the menu") {
    bumpy::App app(15);
    REQUIRE(app.update(bumpy::MenuInput{.down = true}) == bumpy::AppOutcome::running);   // row 1
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);               // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::high_scores);
    REQUIRE(app.high_score_screen().view().mode == bumpy::HighScoreMode::view);

    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);               // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
}

TEST_CASE("game over shows GAME OVER, then the high-score table with the run's score") {
    bumpy::App app(15);
    enter_level(app);

    app.finish_level(bumpy::LevelStatus::quit, 0, 9999);  // out of lives
    REQUIRE(app.screen() == bumpy::Screen::game_over);

    pass_game_over(app);
    REQUIRE(app.screen() == bumpy::Screen::high_scores);
    REQUIRE(app.high_score_screen().view().mode == bumpy::HighScoreMode::entry);
    REQUIRE(app.high_score_screen().view().insert_row >= 0);  // 9999 beats MIKE (500)

    // Release the (absent) key, then fire commits the name; the run resets and returns to menu.
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.lives() == 5);   // run reset after the high-score screen
    REQUIRE(app.score() == 0);
}
```

Replace the existing `TEST_CASE("running out of lives ends the game and resets the run")` body with:

```cpp
TEST_CASE("running out of lives goes to game over then high scores, then resets the run") {
    bumpy::App app(15);
    enter_level(app);

    app.finish_level(bumpy::LevelStatus::quit, 0, 9999);  // 22fc set 928d=0xff
    REQUIRE(app.screen() == bumpy::Screen::game_over);
    pass_game_over(app);
    REQUIRE(app.screen() == bumpy::Screen::high_scores);

    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);               // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // dismiss
    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.lives() == 5);   // run reset
    REQUIRE(app.score() == 0);
    REQUIRE_FALSE(app.is_board_cleared(0));
}
```

Update the tail of `TEST_CASE("a game over after advancing requests a reload of the start world")` — after `app.finish_level(bumpy::LevelStatus::quit, 0, 200);` insert the drive-through before the menu assertions:

```cpp
    app.finish_level(bumpy::LevelStatus::quit, 0, 200);  // out of lives -> game over (200 < 500)
    REQUIRE(app.screen() == bumpy::Screen::game_over);
    pass_game_over(app);
    REQUIRE(app.screen() == bumpy::Screen::high_scores);
    REQUIRE(app.high_score_screen().view().insert_row == -1);  // 200 does not qualify
    REQUIRE(app.update(bumpy::MenuInput{}) == bumpy::AppOutcome::running);               // release
    REQUIRE(app.update(bumpy::MenuInput{.confirm = true}) == bumpy::AppOutcome::running);  // dismiss

    REQUIRE(app.screen() == bumpy::Screen::menu);
    REQUIRE(app.pending_world() == 1);  // reset_run asks the shell to reload world 1
    app.enter_world(1, 15);
    REQUIRE(app.world() == 1);
    REQUIRE(app.lives() == 5);
    REQUIRE(app.score() == 0);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `build/windows-debug/Debug/bumpy_tests.exe "game over shows GAME OVER, then the high-score table with the run's score"`
Expected: build fails — `Screen::game_over` / `App::high_score_screen` do not exist.

- [ ] **Step 3: Extend `app.h`**

Add includes at the top of `src/game/app.h` (with the other game includes):

```cpp
#include "game/high_score_screen.h"
#include "game/high_scores.h"
```

Extend the `Screen` enum:

```cpp
enum class Screen {
    menu,
    map,
    level,
    outro,       // DESSFIN.VEC ending screen after world 9 (FUN_1000_3ed4)
    game_over,   // FUN_1000_11eb: SCORE.VEC + "GAME OVER", timed, then high_scores
    high_scores, // FUN_1000_5681/57e1: the high-score table (+ name entry on game over)
};
```

Add accessors (near the other `[[nodiscard]]` ones):

```cpp
    [[nodiscard]] const HighScoreTable& high_scores() const noexcept { return high_scores_; }
    [[nodiscard]] const HighScoreScreen& high_score_screen() const noexcept {
        return high_score_screen_;
    }
```

Add members (with the other private members):

```cpp
    HighScoreTable high_scores_;          // session table (baked defaults, no persistence)
    HighScoreScreen high_score_screen_;   // transient view/entry screen state
    int game_over_frames_{0};             // frames the GAME OVER screen has been shown
```

- [ ] **Step 4: Extend `app.cpp`**

At the top of `src/game/app.cpp` add the pacing constant in the anonymous/`bumpy` scope (after the includes):

```cpp
namespace {
// How long the timed GAME OVER screen (FUN_1000_11eb, ~100 un-paced commits) is shown
// before auto-advancing to the high-score table. Tuned by eye (~0.5 s at 70 Hz).
constexpr int kGameOverFrames = 35;
}  // namespace
```

In `App::update`, add the game_over / high_scores handling **immediately after the `Screen::outro` block** (before the `cancel_edge` computation), so these screens own their input and cannot bounce a cancel into the menu:

```cpp
    // The timed GAME OVER screen (FUN_1000_11eb): show it for kGameOverFrames, then hand off
    // to the high-score table in entry mode with the run's final score. Input is ignored.
    if (screen_ == Screen::game_over) {
        if (++game_over_frames_ >= kGameOverFrames) {
            high_score_screen_.enter_entry(high_scores_, score_);  // FUN_1000_5681 -> 57e1
            screen_ = Screen::high_scores;
        }
        return AppOutcome::running;
    }

    // The high-score table (FUN_1000_5681). done -> menu; a game-over path (entry mode) also
    // resets the run first (the original returns to a fresh menu after 5681).
    if (screen_ == Screen::high_scores) {
        if (high_score_screen_.update(input) == HighScoreResult::done) {
            const bool from_game_over = high_score_screen_.view().mode == HighScoreMode::entry;
            if (from_game_over) {
                reset_run();
            }
            screen_ = Screen::menu;
            waiting_for_release_ = true;  // guard the menu's cancel until the dismiss key releases
        }
        return AppOutcome::running;
    }
```

In the `Screen::menu` branch, add the `high_scores` case to the `switch (menu_.update(input))`:

```cpp
        case MenuAction::high_scores:
            high_score_screen_.enter_view();  // FUN_1000_5681 from the menu (score 0 -> view)
            screen_ = Screen::high_scores;
            return AppOutcome::running;
```

In `App::finish_level`, replace the `LevelStatus::quit` case body:

```cpp
    case LevelStatus::quit:
        // Out of lives (FUN_1000_22fc set DAT_928d = 0xff) -> the GAME OVER screen
        // (FUN_1000_11eb) then the high-score table (FUN_1000_5681). The run is reset only
        // once the player leaves the high-score screen (see the high_scores branch above).
        screen_ = Screen::game_over;
        game_over_frames_ = 0;
        break;
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build --preset windows-debug` then `build/windows-debug/Debug/bumpy_tests.exe` (full suite — the two rewritten game-over cases and the two new cases must all pass).
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/game/app.h src/game/app.cpp tests/cpp/app_test.cpp
git commit -m "feat: wire high-score + game-over screens into App"
```

---

### Task 6: Shell + main wiring + dev flags

**Files:**
- Modify: `src/platform_sdl3/sdl_app.h` (add `score_screen` param to `run`)
- Modify: `src/platform_sdl3/sdl_app.cpp` (render the two new screens)
- Modify: `src/app/main.cpp` (decode SCORE.VEC, pass it into `run`; add `--render-highscores` / `--render-gameover`)

**Interfaces:**
- Consumes: `render_high_scores`/`render_game_over` (`video/high_score_renderer.h`), `App::high_scores()`/`App::high_score_screen()`.

- [ ] **Step 1: Extend the shell signature and render branches**

In `src/platform_sdl3/sdl_app.h`, add a `score_screen` span to `run` (next to `outro_screen`):

```cpp
    int run(App& app, const MenuRenderer& menu_renderer, const std::filesystem::path& asset_root,
            WorldResources world, std::span<const std::uint8_t> sprite_bank, const Font& font,
            std::span<const std::uint8_t> outro_screen, std::span<const std::uint8_t> score_screen,
            IndexedFramebuffer& frame);
```

In `src/platform_sdl3/sdl_app.cpp`:
- add `#include "video/high_score_renderer.h"` with the other video includes;
- update the `run(...)` definition signature to match (add `std::span<const std::uint8_t> score_screen` after `outro_screen`);
- in the render dispatch (the `if (app.screen() == Screen::menu) … else if … Screen::outro … else render_level();` chain), add two branches before the final `else`:

```cpp
        } else if (app.screen() == Screen::game_over) {
            // FUN_1000_11eb: SCORE.VEC + "GAME OVER". The level->game_over darken already
            // wiped in via the screen-change transition.
            render_game_over(score_screen, sprite_bank, frame);
        } else if (app.screen() == Screen::high_scores) {
            // FUN_1000_5681/57e1: the table (+ blinking caret during name entry).
            render_high_scores(score_screen, app.high_scores(), sprite_bank,
                               app.high_score_screen().view(), frame);
```

(The existing `half_rate` computation already leaves these at the full retrace rate, like the menu — no change needed there.)

- [ ] **Step 2: Load SCORE.VEC in main and pass it through**

In `src/app/main.cpp`, in `run_sdl_menu`, decode SCORE.VEC once (world-independent, like the outro) and pass it into `run`:

```cpp
    const auto outro = bumpy::decode_vec_resource(asset_root / "DESSFIN.VEC");
    // The HIGH-SCORE / GAME OVER backdrop (SCORE.VEC, MENU index 3). World-independent.
    const auto score_screen = bumpy::decode_vec_resource(asset_root / "SCORE.VEC");
```

and update the `sdl.run(...)` call to pass `score_screen.decoded_bytes()` after `outro.decoded_bytes()`:

```cpp
    return sdl.run(app, renderer, asset_root, std::move(world), sprite_bank.bytes(), font,
                   outro.decoded_bytes(), score_screen.decoded_bytes(), frame);
```

- [ ] **Step 3: Add the dev flags**

In `src/app/main.cpp`, add two headless dumpers (near `render_outro_to_bmp`):

```cpp
// Dump the HIGH-SCORE table (FUN_1000_5681/57e1) through the shared renderer for by-eye
// checking. With an optional `insert_score`, insert it and show the name-entry caret.
int render_highscores_to_bmp(const std::filesystem::path& asset_root,
                             const std::filesystem::path& score_path,
                             const std::filesystem::path& out_path, long insert_score) {
    const auto score_res = bumpy::decode_vec_resource(score_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::HighScoreTable table;
    bumpy::HighScoreScreenView view{};  // view mode
    if (insert_score > 0) {
        view.mode = bumpy::HighScoreMode::entry;
        view.insert_row = table.insert(static_cast<std::uint32_t>(insert_score));
        view.cursor_col = 0;
        view.caret_visible = true;
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_high_scores(score_res.decoded_bytes(), table, bank.bytes(), view, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

int render_gameover_to_bmp(const std::filesystem::path& asset_root,
                           const std::filesystem::path& score_path,
                           const std::filesystem::path& out_path) {
    const auto score_res = bumpy::decode_vec_resource(score_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_game_over(score_res.decoded_bytes(), bank.bytes(), frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}
```

Add the `#include "game/high_scores.h"`, `#include "game/high_score_screen.h"`, and `#include "video/high_score_renderer.h"` at the top, and wire the argv dispatch in `main` (next to the `--render-outro` handler):

```cpp
        if ((argc == 4 || argc == 5) && std::string_view(argv[1]) == "--render-highscores") {
            // --render-highscores <SCORE.VEC> <out.bmp> [insert_score]
            const long insert = argc == 5 ? std::stol(argv[4]) : 0;
            return render_highscores_to_bmp(asset_root, argv[2], argv[3], insert);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--render-gameover") {
            // --render-gameover <SCORE.VEC> <out.bmp>
            return render_gameover_to_bmp(asset_root, argv[2], argv[3]);
        }
```

- [ ] **Step 4: Build and verify by eye + full suite**

Run:
```
cmake --build --preset windows-debug
build/windows-debug/Debug/bumpy_port.exe --render-gameover SCORE.VEC analysis/generated/gameover.bmp
build/windows-debug/Debug/bumpy_port.exe --render-highscores SCORE.VEC analysis/generated/highscores.bmp
build/windows-debug/Debug/bumpy_port.exe --render-highscores SCORE.VEC analysis/generated/highscores_entry.bmp 2000000
build/windows-debug/Debug/bumpy_tests.exe
python tools/assets/manifest.py verify
```
Expected: the three BMPs show the SCORE.VEC backdrop with the "GAME OVER" text, the 7-entry table (BIG JIM … MIKE), and a table with an `AAAAAAAA`/caret row; the test suite passes; originals verify clean. Then launch `build/windows-debug/Debug/bumpy_port.exe`, select menu row 1 → the table appears, any key returns; play and lose all lives → GAME OVER flash → table (→ name editor if the score qualifies) → menu.

- [ ] **Step 5: Commit**

```bash
git add src/platform_sdl3/sdl_app.h src/platform_sdl3/sdl_app.cpp src/app/main.cpp
git commit -m "feat: render high-score + game-over screens in the shell; dev dumpers"
```

---

### Task 7: Update PROJECT_STATUS.md

**Files:**
- Modify: `PROJECT_STATUS.md` (correct the `0d9d/0f7a/11eb` mislabel; record the high-score screen as done)

- [ ] **Step 1: Correct the mislabel and add a status section**

Replace the "menu sub-screens (HIGH-SCORE / PASSWORD)" placeholder bullets with a note that the **HIGH-SCORE screen is implemented** (`FUN_1000_5681`/`57e1`/`59d3` + `FUN_1000_11eb`; `SCORE.VEC`; baked table, no persistence; menu row 1 + game-over entry with name editor), and correct that `0d9d/0f7a/11eb` are the PASSWORD/GAME-OVER screens (PASSWORD still the only remaining menu sub-screen). Add a dated "Stage 3 high-score screen" section mirroring the existing ones, citing the spec/plan and the `--render-highscores`/`--render-gameover` flags.

- [ ] **Step 2: Commit**

```bash
git add PROJECT_STATUS.md docs/superpowers/specs/2026-07-05-high-score-screen-design.md docs/superpowers/plans/2026-07-05-high-score-screen.md
git commit -m "docs: record the high-score screen; correct the 0d9d/0f7a/11eb labels"
```

---

## Self-Review

**Spec coverage:** menu row 1 view (Task 4/5), game-over → GAME OVER → table (Task 5/6, `11eb`), name-entry editor (Task 2), baked defaults / no persistence (Task 1), SCORE.VEC backdrop + glyph text (Task 3), two-darken flow (free via `Screen` changes, Task 5/6), pacing constants (Task 2/5), dev flags + tests (Task 3/6). Victory path unchanged (untouched). All spec sections map to a task.

**Placeholder scan:** no TBD/TODO; every code step carries complete code. The one deliberate implementer-verify note (exact `kNameCycle` order in Task 2) ships a concrete, grounded default and a test — not a placeholder.

**Type consistency:** `HighScoreEntry`/`HighScoreTable`/`kHighScoreCount`/`kHighScoreNameLength` (Task 1) used verbatim in Tasks 2/3/5; `HighScoreScreen`/`HighScoreScreenView`/`HighScoreMode`/`HighScoreResult` (Task 2) used verbatim in Tasks 3/5/6; `high_score_glyph_frame`/`render_high_scores`/`render_game_over` (Task 3) used verbatim in Task 6; `Screen::game_over`/`Screen::high_scores`/`MenuAction::high_scores`/`App::high_scores()`/`App::high_score_screen()` consistent across Tasks 4/5/6.

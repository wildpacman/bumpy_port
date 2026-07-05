# Startup Splash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the original startup-only `BUMPRESE.VEC` splash screen before the menu.

**Architecture:** `App` owns the startup state by adding `Screen::splash` as the initial screen. SDL renders that screen with the existing full-screen `screen_image` path, and menu resources own the decoded `BUMPRESE.VEC` asset.

**Tech Stack:** C++20, Catch2, CMake, SDL3 shell, existing VEC/screen-image decoders.

---

### Task 1: App State Machine

**Files:**
- Modify: `src/game/app.h`
- Modify: `src/game/app.cpp`
- Test: `tests/cpp/app_test.cpp`

- [x] **Step 1: Write failing tests**

Add tests asserting that a new `App` starts on `Screen::splash`, confirm moves it to
`Screen::menu`, and a held confirm does not bounce into `Screen::map`.

- [x] **Step 2: Verify RED**

Run: `cmake --build build --config Release --target bumpy_tests` then `ctest --test-dir build -C Release --output-on-failure`.
Expected: Catch2 fails because the app starts on `Screen::menu`.

- [x] **Step 3: Implement minimal state**

Add `splash` to `Screen`, initialize `screen_` with it, and handle the splash branch
at the start of `App::update`.

- [x] **Step 4: Verify GREEN**

Run the same build and test commands. Expected: the new app tests pass.

### Task 2: Splash Resource and Renderer

**Files:**
- Modify: `src/resources/menu_resources.h`
- Modify: `src/resources/menu_resources.cpp`
- Modify: `src/platform_sdl3/sdl_app.h`
- Modify: `src/platform_sdl3/sdl_app.cpp`
- Modify: `src/app/main.cpp`
- Test: `tests/cpp/menu_resources_test.cpp`

- [x] **Step 1: Write failing resource test**

Add a test asserting `MenuResources::load_from(".").splash` is a screen-format
decoded image.

- [x] **Step 2: Verify RED**

Run: `cmake --build build --config Release --target bumpy_tests` then `ctest --test-dir build -C Release --output-on-failure`.
Expected: compile failure or test failure because `splash` is not present.

- [x] **Step 3: Implement resource loading and SDL draw path**

Load `BUMPRESE.VEC` in `MenuResources`; pass the decoded bytes to `SdlApp::run`;
when `app.screen() == Screen::splash`, apply its palette and draw the screen image.

- [x] **Step 4: Verify GREEN**

Run the same build and test commands. Expected: all C++ tests pass.

### Task 3: Final Verification

**Files:**
- Inspect: `git diff`

- [x] **Step 1: Run full verification**

Run: `ctest --test-dir build -C Release --output-on-failure`.
Expected: all tests pass.

- [x] **Step 2: Inspect changes**

Run: `git diff -- src tests docs`.
Expected: only splash-related changes are present.

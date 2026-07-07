# 3D Render Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An optional "diorama" 3D presentation of the in-level playfield (Alt+3): the DEC mural as a blurred back wall, BUM entities as extruded slabs / crisp billboards, ball-following parallax and spotlight — same pixels, no upscaling — per `docs/superpowers/specs/2026-07-08-3d-render-mode-design.md`.

**Architecture:** Presentation moves from `SDL_Renderer` to an in-house OpenGL 3.3 core presenter with two paths: a flat path (default, all screens — one quad with a pixel-art shader, byte-identical 320x200 composition) and a 3D path (in-level only). Scene extraction is CPU-side in `src/video3d/` (unit-testable, lives in `bumpy_core`); GL code lives in `src/platform_gl3/` (linked into `bumpy_platform_sdl3`, verified via headless dump tools). The old SDL_Renderer path is kept as a runtime fallback when GL 3.3 is unavailable.

**Tech Stack:** C++20, SDL3 (window/context/input), OpenGL 3.3 core loaded via `SDL_GL_GetProcAddress` (SDL ships the GL headers — **no third-party loader**), GLSL 330 shader files, Catch2 tests, CMake presets (`windows-debug`).

## Global Constraints (from the spec)

- Game logic, timing, integer positions, PRNG, and the `--render-*` RE dump outputs are **never** modified.
- Game art is drawn pixel-for-pixel: no upscaling, no filtering of art pixels (`GL_NEAREST` on sprites); the wall blur is a baked *effect* over the real mural.
- No scene elements that were not on the DOS screen: no floor, no particles. Allowed: effects (blur/light/vignette/shadows) and side faces extruded from a sprite's own edge pixels.
- Camera: near-frontal diorama, light parallax following the ball only.
- 3D mode is optional; the flat path is default. GL/shader failure → log to stderr, run flat, Alt+3 disabled — the game never dies because of 3D.
- Switching: Alt+3 (instant, no transition animation), `--render3d` CLI flag, `bumpy_port.cfg` next to the exe persisting render mode + aspect + fullscreen. Flag overrides config.
- `feat/hd-render-mode` is not merged, not deleted, not depended on. No xBRZ.
- Non-level screens render flat even in 3D mode (dressing them is phase 2, out of this plan).

**Build/test commands used throughout:**
- Configure (once): `cmake --preset windows-debug`
- Build: `cmake --build --preset windows-debug`
- Test: `ctest --preset windows-debug` (or `build/windows-debug/Debug/bumpy_tests.exe "<test name>"` from the project root — tests assume CWD = project root)

---

### Task 1: Letterbox viewport math

The GL presenter replaces `SDL_SetRenderLogicalPresentation`; the letterbox rect becomes our own pure function (also used by the 3D path and offscreen tools).

**Files:**
- Create: `src/video/viewport.h`, `src/video/viewport.cpp`
- Modify: `CMakeLists.txt` (add `src/video/viewport.cpp` to `bumpy_core`; add `tests/cpp/viewport_test.cpp` to `bumpy_tests`)
- Test: `tests/cpp/viewport_test.cpp`

**Interfaces:**
- Produces: `struct Viewport { int x, y, w, h; }` and `Viewport compute_letterbox_viewport(int window_w, int window_h, int logical_w, int logical_h) noexcept` in namespace `bumpy` — consumed by Tasks 4, 12, 13.

- [ ] **Step 1: Write the failing test** — `tests/cpp/viewport_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "video/viewport.h"

using bumpy::compute_letterbox_viewport;

TEST_CASE("exact fit fills the window") {
    const auto vp = compute_letterbox_viewport(640, 400, 320, 200);
    REQUIRE(vp.x == 0);
    REQUIRE(vp.y == 0);
    REQUIRE(vp.w == 640);
    REQUIRE(vp.h == 400);
}

TEST_CASE("wider window letterboxes left and right") {
    // 16:9 1920x1080 window, 16:10 logical: height-limited, 1728x1080 centred.
    const auto vp = compute_letterbox_viewport(1920, 1080, 320, 200);
    REQUIRE(vp.h == 1080);
    REQUIRE(vp.w == 1728);
    REQUIRE(vp.x == 96);
    REQUIRE(vp.y == 0);
}

TEST_CASE("taller window letterboxes top and bottom") {
    const auto vp = compute_letterbox_viewport(640, 600, 320, 200);
    REQUIRE(vp.w == 640);
    REQUIRE(vp.h == 400);
    REQUIRE(vp.x == 0);
    REQUIRE(vp.y == 100);
}

TEST_CASE("4:3 logical (Alt+A CRT aspect) letterboxes a 16:10 window") {
    const auto vp = compute_letterbox_viewport(960, 600, 320, 240);
    REQUIRE(vp.h == 600);
    REQUIRE(vp.w == 800);
    REQUIRE(vp.x == 80);
}

TEST_CASE("degenerate sizes yield an empty viewport") {
    const auto vp = compute_letterbox_viewport(0, 600, 320, 200);
    REQUIRE(vp.w == 0);
    REQUIRE(vp.h == 0);
}
```

- [ ] **Step 2: Register + run to verify it fails.** Add to `CMakeLists.txt`: `src/video/viewport.cpp` in the `bumpy_core` sources (after `src/video/hud.cpp`), `tests/cpp/viewport_test.cpp` in `bumpy_tests`. Run `cmake --build --preset windows-debug` — expected: compile error (`viewport.h` not found).

- [ ] **Step 3: Implement** — `src/video/viewport.h`:

```cpp
#pragma once

namespace bumpy {

struct Viewport {
    int x{};
    int y{};
    int w{};
    int h{};
};

// The largest logical_w:logical_h rectangle that fits (window_w, window_h),
// centred -- the letterbox rect SDL_LOGICAL_PRESENTATION_LETTERBOX would pick.
// Degenerate inputs (any dimension <= 0) return an empty viewport.
[[nodiscard]] Viewport compute_letterbox_viewport(int window_w, int window_h,
                                                  int logical_w, int logical_h) noexcept;

}  // namespace bumpy
```

`src/video/viewport.cpp`:

```cpp
#include "video/viewport.h"

#include <algorithm>
#include <cmath>

namespace bumpy {

Viewport compute_letterbox_viewport(int window_w, int window_h,
                                    int logical_w, int logical_h) noexcept {
    if (window_w <= 0 || window_h <= 0 || logical_w <= 0 || logical_h <= 0) {
        return {};
    }
    const double scale = std::min(static_cast<double>(window_w) / logical_w,
                                  static_cast<double>(window_h) / logical_h);
    const int w = std::max(1, static_cast<int>(std::lround(logical_w * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(logical_h * scale)));
    return {(window_w - w) / 2, (window_h - h) / 2, w, h};
}

}  // namespace bumpy
```

- [ ] **Step 4: Run tests to verify they pass.** `cmake --build --preset windows-debug && ctest --preset windows-debug` — expected: all PASS.

- [ ] **Step 5: Commit.** `git add -A && git commit -m "feat(video): letterbox viewport math for the GL presenter"`

---

### Task 2: PortConfig — the port's first on-disk persistence

**Files:**
- Create: `src/core/port_config.h`, `src/core/port_config.cpp`
- Modify: `CMakeLists.txt` (`src/core/port_config.cpp` → `bumpy_core`; `tests/cpp/port_config_test.cpp` → `bumpy_tests`)
- Test: `tests/cpp/port_config_test.cpp`

**Interfaces:**
- Produces (namespace `bumpy`):
  - `struct PortConfig { bool render3d = false; bool square_pixels = true; bool fullscreen = false; }`
  - `PortConfig parse_port_config(std::string_view text) noexcept`
  - `std::string serialize_port_config(const PortConfig& config)`
  - `PortConfig load_port_config(const std::filesystem::path& path) noexcept` — missing/unreadable/garbage file → defaults
  - `bool save_port_config(const std::filesystem::path& path, const PortConfig& config) noexcept` — best-effort write, false on failure
- Consumed by Task 7 (main.cpp / SdlApp).

- [ ] **Step 1: Write the failing test** — `tests/cpp/port_config_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/port_config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using bumpy::PortConfig;

TEST_CASE("defaults: flat render, square pixels, windowed") {
    const PortConfig c{};
    REQUIRE_FALSE(c.render3d);
    REQUIRE(c.square_pixels);
    REQUIRE_FALSE(c.fullscreen);
}

TEST_CASE("serialize/parse round-trips every field") {
    PortConfig c;
    c.render3d = true;
    c.square_pixels = false;
    c.fullscreen = true;
    const auto back = bumpy::parse_port_config(bumpy::serialize_port_config(c));
    REQUIRE(back.render3d);
    REQUIRE_FALSE(back.square_pixels);
    REQUIRE(back.fullscreen);
}

TEST_CASE("parse ignores unknown keys, blank lines and comments; partial file keeps defaults") {
    const auto c = bumpy::parse_port_config("# comment\n\nrender3d=1\nfuture_key=42\n");
    REQUIRE(c.render3d);
    REQUIRE(c.square_pixels);   // untouched default
    REQUIRE_FALSE(c.fullscreen);
}

TEST_CASE("parse tolerates garbage values") {
    const auto c = bumpy::parse_port_config("render3d=banana\nsquare_pixels=\n=1\n");
    REQUIRE_FALSE(c.render3d);
    REQUIRE(c.square_pixels);
}

TEST_CASE("load returns defaults for a missing file; save/load round-trips") {
    const auto dir = std::filesystem::temp_directory_path();
    const auto path = dir / "bumpy_port_config_test.cfg";
    std::filesystem::remove(path);

    const auto missing = bumpy::load_port_config(path);
    REQUIRE_FALSE(missing.render3d);

    PortConfig c;
    c.render3d = true;
    c.fullscreen = true;
    REQUIRE(bumpy::save_port_config(path, c));
    const auto back = bumpy::load_port_config(path);
    REQUIRE(back.render3d);
    REQUIRE(back.fullscreen);
    std::filesystem::remove(path);
}
```

- [ ] **Step 2: Register in CMake, build, verify the test fails to compile.**

- [ ] **Step 3: Implement** — `src/core/port_config.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace bumpy {

// Port-level presentation settings persisted in bumpy_port.cfg next to the exe.
// This is deliberately the port's ONLY on-disk persistence (high scores stay
// session-only like the original). Simple key=value lines; unknown keys are
// ignored so future versions can add fields without breaking older builds.
struct PortConfig {
    bool render3d = false;      // Alt+3 diorama mode
    bool square_pixels = true;  // Alt+A: true = 16:10 (logical 200), false = 4:3 (240)
    bool fullscreen = false;    // Alt+Enter
};

[[nodiscard]] PortConfig parse_port_config(std::string_view text) noexcept;
[[nodiscard]] std::string serialize_port_config(const PortConfig& config);
// Missing/unreadable/garbage file -> defaults; never throws.
[[nodiscard]] PortConfig load_port_config(const std::filesystem::path& path) noexcept;
// Best-effort write; false on failure (callers log, the game keeps running).
bool save_port_config(const std::filesystem::path& path, const PortConfig& config) noexcept;

}  // namespace bumpy
```

`src/core/port_config.cpp`:

```cpp
#include "core/port_config.h"

#include <fstream>
#include <sstream>

namespace bumpy {

namespace {

// "1"/"0" only; anything else leaves `out` untouched (tolerate hand-edits).
void parse_bool(std::string_view value, bool& out) {
    if (value == "1") {
        out = true;
    } else if (value == "0") {
        out = false;
    }
}

}  // namespace

PortConfig parse_port_config(std::string_view text) noexcept {
    PortConfig config;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line = text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) {
            continue;
        }
        const std::string_view key = line.substr(0, eq);
        const std::string_view value = line.substr(eq + 1);
        if (key == "render3d") {
            parse_bool(value, config.render3d);
        } else if (key == "square_pixels") {
            parse_bool(value, config.square_pixels);
        } else if (key == "fullscreen") {
            parse_bool(value, config.fullscreen);
        }
    }
    return config;
}

std::string serialize_port_config(const PortConfig& config) {
    std::ostringstream out;
    out << "# Bumpy's Arcade Fantasy port settings (auto-written; hand-edits are kept\n"
        << "# for known keys, unknown keys are ignored)\n"
        << "render3d=" << (config.render3d ? 1 : 0) << '\n'
        << "square_pixels=" << (config.square_pixels ? 1 : 0) << '\n'
        << "fullscreen=" << (config.fullscreen ? 1 : 0) << '\n';
    return out.str();
}

PortConfig load_port_config(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return {};
        }
        std::ostringstream text;
        text << in.rdbuf();
        return parse_port_config(text.str());
    } catch (...) {
        return {};
    }
}

bool save_port_config(const std::filesystem::path& path, const PortConfig& config) noexcept {
    try {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << serialize_port_config(config);
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}

}  // namespace bumpy
```

- [ ] **Step 4: Build + run tests — PASS.**

- [ ] **Step 5: Commit.** `git commit -m "feat(core): PortConfig -- persisted render/aspect/fullscreen settings"`

---

### Task 3: GL 3.3 foundation — function loader + shader/texture/FBO helpers

No third-party code: SDL3 ships `SDL_opengl.h` / `SDL_opengl_glext.h` (all GL types and `PFNGL*` typedefs). GL 1.1 entry points (glClear, glTexImage2D, glViewport, glGenTextures, glDrawArrays, glReadPixels, glEnable, glBlendFunc, glDepthFunc, glDepthMask, glPixelStorei…) are exported by `opengl32.dll` and called directly; everything newer is loaded via `SDL_GL_GetProcAddress`.

**Files:**
- Create: `src/platform_gl3/gl33.h`, `src/platform_gl3/gl33.cpp`, `src/platform_gl3/gl_util.h`, `src/platform_gl3/gl_util.cpp`
- Modify: `CMakeLists.txt` — add both `.cpp` to `bumpy_platform_sdl3`; change its link line to `target_link_libraries(bumpy_platform_sdl3 PUBLIC bumpy_core SDL3::SDL3 opengl32)`

**Interfaces:**
- Produces (namespace `bumpy`):
  - `struct Gl33` — loaded post-1.1 function pointers, member names without the `gl` prefix (e.g. `gl.CreateShader(...)`)
  - `bool load_gl33(Gl33& gl)` — requires a current context; false if any pointer is missing
  - `GLuint compile_shader(const Gl33& gl, GLenum type, std::string_view source)` — throws `std::runtime_error` with the info log
  - `GLuint link_program(const Gl33& gl, std::string_view vert_src, std::string_view frag_src)` — throws with log
  - `GLuint make_rgba_texture(std::span<const std::uint8_t> rgba, int w, int h, bool linear_filter)` — NEAREST default, CLAMP_TO_EDGE
  - `struct OffscreenTarget { GLuint fbo, color, depth; int w, h; }`, `OffscreenTarget make_offscreen_target(const Gl33& gl, int w, int h)` (throws on incomplete FBO), `void destroy_offscreen_target(const Gl33& gl, OffscreenTarget& t)`, `std::vector<std::uint8_t> read_target_rgba(const Gl33& gl, const OffscreenTarget& t)` — rows **top-to-bottom**
- Consumed by Tasks 4, 5, 12.

- [ ] **Step 1: Write `src/platform_gl3/gl33.h`:**

```cpp
#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

// Every post-GL-1.1 function the port uses, as (typedef, Name) pairs. GL 1.1 is
// exported by opengl32.dll and called directly; these are loaded per-context via
// SDL_GL_GetProcAddress. Supersedes the spec's "vendor glad": SDL already ships
// the headers, so no third-party loader is needed.
#define BUMPY_GL33_FUNCS(X) \
    X(PFNGLCREATESHADERPROC, CreateShader) \
    X(PFNGLSHADERSOURCEPROC, ShaderSource) \
    X(PFNGLCOMPILESHADERPROC, CompileShader) \
    X(PFNGLGETSHADERIVPROC, GetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC, DeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram) \
    X(PFNGLATTACHSHADERPROC, AttachShader) \
    X(PFNGLLINKPROGRAMPROC, LinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, UseProgram) \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, Uniform1i) \
    X(PFNGLUNIFORM1FPROC, Uniform1f) \
    X(PFNGLUNIFORM2FPROC, Uniform2f) \
    X(PFNGLUNIFORM3FPROC, Uniform3f) \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLGENBUFFERSPROC, GenBuffers) \
    X(PFNGLBINDBUFFERPROC, BindBuffer) \
    X(PFNGLBUFFERDATAPROC, BufferData) \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer) \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers)

namespace bumpy {

struct Gl33 {
#define BUMPY_GL33_DECL(type, name) type name{};
    BUMPY_GL33_FUNCS(BUMPY_GL33_DECL)
#undef BUMPY_GL33_DECL
};

// Loads every pointer above from the CURRENT GL context. False if any is missing
// (caller treats that as "no GL 3.3" and falls back to the SDL_Renderer path).
[[nodiscard]] bool load_gl33(Gl33& gl);

}  // namespace bumpy
```

- [ ] **Step 2: Write `src/platform_gl3/gl33.cpp`:**

```cpp
#include "platform_gl3/gl33.h"

namespace bumpy {

bool load_gl33(Gl33& gl) {
    bool ok = true;
#define BUMPY_GL33_LOAD(type, name)                                          \
    gl.name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name));     \
    ok = ok && gl.name != nullptr;
    BUMPY_GL33_FUNCS(BUMPY_GL33_LOAD)
#undef BUMPY_GL33_LOAD
    return ok;
}

}  // namespace bumpy
```

- [ ] **Step 3: Write `src/platform_gl3/gl_util.h`:**

```cpp
#pragma once

#include "platform_gl3/gl33.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bumpy {

// Compile one shader stage / link a program; throws std::runtime_error carrying
// the GL info log on failure (callers turn that into "3D disabled", never a crash).
[[nodiscard]] GLuint compile_shader(const Gl33& gl, GLenum type, std::string_view source);
[[nodiscard]] GLuint link_program(const Gl33& gl, std::string_view vert_src,
                                  std::string_view frag_src);

// Tightly-packed RGBA8 (rows top-to-bottom) -> texture. NEAREST by default (art is
// never filtered); linear_filter=true for the pre-blurred wall.
[[nodiscard]] GLuint make_rgba_texture(std::span<const std::uint8_t> rgba, int w, int h,
                                       bool linear_filter = false);

// Offscreen RGBA8 + depth24 render target for headless parity checks and dumps.
struct OffscreenTarget {
    GLuint fbo{};
    GLuint color{};
    GLuint depth{};
    int w{};
    int h{};
};
[[nodiscard]] OffscreenTarget make_offscreen_target(const Gl33& gl, int w, int h);
void destroy_offscreen_target(const Gl33& gl, OffscreenTarget& target);
// Reads the bound-independent target as tight RGBA rows TOP-TO-BOTTOM
// (glReadPixels returns bottom-up; this flips).
[[nodiscard]] std::vector<std::uint8_t> read_target_rgba(const Gl33& gl,
                                                         const OffscreenTarget& target);

}  // namespace bumpy
```

- [ ] **Step 4: Write `src/platform_gl3/gl_util.cpp`:**

```cpp
#include "platform_gl3/gl_util.h"

#include <stdexcept>
#include <string>

namespace bumpy {

GLuint compile_shader(const Gl33& gl, GLenum type, std::string_view source) {
    const GLuint shader = gl.CreateShader(type);
    const GLchar* src = source.data();
    const GLint len = static_cast<GLint>(source.size());
    gl.ShaderSource(shader, 1, &src, &len);
    gl.CompileShader(shader);
    GLint ok = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLchar log[2048] = {};
        GLsizei written = 0;
        gl.GetShaderInfoLog(shader, sizeof(log) - 1, &written, log);
        gl.DeleteShader(shader);
        throw std::runtime_error("shader compile failed: " + std::string(log, written));
    }
    return shader;
}

GLuint link_program(const Gl33& gl, std::string_view vert_src, std::string_view frag_src) {
    const GLuint vs = compile_shader(gl, GL_VERTEX_SHADER, vert_src);
    GLuint fs = 0;
    try {
        fs = compile_shader(gl, GL_FRAGMENT_SHADER, frag_src);
    } catch (...) {
        gl.DeleteShader(vs);
        throw;
    }
    const GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vs);
    gl.AttachShader(program, fs);
    gl.LinkProgram(program);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = GL_FALSE;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLchar log[2048] = {};
        GLsizei written = 0;
        gl.GetProgramInfoLog(program, sizeof(log) - 1, &written, log);
        gl.DeleteProgram(program);
        throw std::runtime_error("program link failed: " + std::string(log, written));
    }
    return program;
}

GLuint make_rgba_texture(std::span<const std::uint8_t> rgba, int w, int h, bool linear_filter) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    const GLint filter = linear_filter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

OffscreenTarget make_offscreen_target(const Gl33& gl, int w, int h) {
    OffscreenTarget target;
    target.w = w;
    target.h = h;
    glGenTextures(1, &target.color);
    glBindTexture(GL_TEXTURE_2D, target.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.GenRenderbuffers(1, &target.depth);
    gl.BindRenderbuffer(GL_RENDERBUFFER, target.depth);
    gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    gl.GenFramebuffers(1, &target.fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            target.color, 0);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                               target.depth);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        destroy_offscreen_target(gl, target);
        throw std::runtime_error("offscreen framebuffer incomplete");
    }
    return target;
}

void destroy_offscreen_target(const Gl33& gl, OffscreenTarget& target) {
    if (target.fbo != 0) {
        gl.DeleteFramebuffers(1, &target.fbo);
    }
    if (target.depth != 0) {
        gl.DeleteRenderbuffers(1, &target.depth);
    }
    if (target.color != 0) {
        glDeleteTextures(1, &target.color);
    }
    target = {};
}

std::vector<std::uint8_t> read_target_rgba(const Gl33& gl, const OffscreenTarget& target) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    std::vector<std::uint8_t> bottom_up(
        static_cast<std::size_t>(target.w) * target.h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, target.w, target.h, GL_RGBA, GL_UNSIGNED_BYTE, bottom_up.data());
    std::vector<std::uint8_t> top_down(bottom_up.size());
    const std::size_t stride = static_cast<std::size_t>(target.w) * 4;
    for (int y = 0; y < target.h; ++y) {
        const std::size_t src = static_cast<std::size_t>(target.h - 1 - y) * stride;
        std::copy_n(bottom_up.data() + src, stride, top_down.data() + y * stride);
    }
    return top_down;
}

}  // namespace bumpy
```

- [ ] **Step 5: CMake — add both `.cpp` to `bumpy_platform_sdl3`, link `opengl32`. Build — expected: clean compile** (no runtime test yet; the parity tool in Task 5 exercises all of this).

- [ ] **Step 6: Commit.** `git commit -m "feat(gl3): GL 3.3 loader over SDL headers + shader/texture/FBO helpers"`

---

### Task 4: GlPresenter — the flat path

**Files:**
- Create: `src/platform_gl3/gl_presenter.h`, `src/platform_gl3/gl_presenter.cpp`
- Modify: `CMakeLists.txt` (`gl_presenter.cpp` → `bumpy_platform_sdl3`)

**Interfaces:**
- Consumes: `Gl33`/`load_gl33`, `link_program`, `compute_letterbox_viewport`, `IndexedFramebuffer::to_rgba()` (packs `r|g<<8|b<<16|a<<24` — little-endian byte order r,g,b,a = `GL_RGBA`+`GL_UNSIGNED_BYTE`).
- Produces (namespace `bumpy`):
  - `class GlPresenter { explicit GlPresenter(SDL_Window* window); const Gl33& gl() const noexcept; void present_flat(const IndexedFramebuffer& frame, int logical_h); std::vector<std::uint8_t> render_flat_offscreen(const IndexedFramebuffer& frame, int w, int h, int logical_h); }` — ctor throws `std::runtime_error` if context/loader/shader fails. Window must have `SDL_WINDOW_OPENGL` and the caller must have set the 3.3 core attributes **before** creating the window.
- Consumed by Tasks 5, 6, 12.

- [ ] **Step 1: Write `src/platform_gl3/gl_presenter.h`:**

```cpp
#pragma once

#include <SDL3/SDL.h>

#include "core/indexed_framebuffer.h"
#include "platform_gl3/gl33.h"

#include <cstdint>
#include <vector>

namespace bumpy {

// OpenGL 3.3 presentation of the composed 320x200 indexed frame: one letterboxed
// quad with a pixel-art scaling shader (crisp interior, <=1px anti-aliased pixel
// edges -- the GL twin of SDL_SCALEMODE_PIXELART). Also owns the GL context that
// the 3D scene renderer (Task 12) draws with.
class GlPresenter {
public:
    // Creates the GL 3.3 core context on `window` (created with SDL_WINDOW_OPENGL
    // and the 3.3 core attributes) and compiles the flat-path shader. Throws
    // std::runtime_error on any failure; the caller falls back to SDL_Renderer.
    explicit GlPresenter(SDL_Window* window);
    ~GlPresenter();
    GlPresenter(const GlPresenter&) = delete;
    GlPresenter& operator=(const GlPresenter&) = delete;

    [[nodiscard]] const Gl33& gl() const noexcept { return gl_; }

    // Upload `frame` and draw it letterboxed at logical 320 x logical_h
    // (200 = 16:10 square pixels, 240 = 4:3 CRT) into the backbuffer + swap.
    void present_flat(const IndexedFramebuffer& frame, int logical_h);

    // The same flat draw into an offscreen (w x h) target; returns RGBA rows
    // top-to-bottom. For --present-parity and headless dumps.
    [[nodiscard]] std::vector<std::uint8_t> render_flat_offscreen(
        const IndexedFramebuffer& frame, int w, int h, int logical_h);

private:
    void upload_frame(const IndexedFramebuffer& frame);
    void draw_flat(const IndexedFramebuffer& frame, int target_w, int target_h,
                   int logical_h);

    SDL_Window* window_{};
    SDL_GLContext context_{};
    Gl33 gl_{};
    GLuint program_{};
    GLuint vao_{};
    GLuint vbo_{};
    GLuint frame_tex_{};
    GLint u_tex_{};
    GLint u_tex_size_{};
};

}  // namespace bumpy
```

- [ ] **Step 2: Write `src/platform_gl3/gl_presenter.cpp`:**

```cpp
#include "platform_gl3/gl_presenter.h"

#include "platform_gl3/gl_util.h"
#include "video/viewport.h"

#include <stdexcept>

namespace {

// Pixel-art scaling: sample on a LINEAR texture but snap UVs so that pixels stay
// crisp except a <=1-screen-pixel ramp at texel boundaries. At integer scales the
// clamp saturates at +-0.5 texel = exact texel centers, i.e. bit-exact nearest.
constexpr const char* kFlatVert = R"GLSL(#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFlatFrag = R"GLSL(#version 330 core
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
void main() {
    vec2 st = v_uv * u_tex_size;
    vec2 seam = floor(st + 0.5);
    vec2 uv = seam + clamp((st - seam) / fwidth(st), -0.5, 0.5);
    o_color = texture(u_tex, uv / u_tex_size);
}
)GLSL";

}  // namespace

namespace bumpy {

GlPresenter::GlPresenter(SDL_Window* window) : window_(window) {
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_GL_MakeCurrent(window_, context_)) {
        SDL_GL_DestroyContext(context_);
        throw std::runtime_error(SDL_GetError());
    }
    // The run loop self-paces on the VGA retrace clock; vsync would fight it.
    SDL_GL_SetSwapInterval(0);
    if (!load_gl33(gl_)) {
        SDL_GL_DestroyContext(context_);
        throw std::runtime_error("OpenGL 3.3 functions unavailable");
    }
    program_ = link_program(gl_, kFlatVert, kFlatFrag);
    u_tex_ = gl_.GetUniformLocation(program_, "u_tex");
    u_tex_size_ = gl_.GetUniformLocation(program_, "u_tex_size");

    // Fullscreen quad: pos.xy in NDC, uv with v=0 at the TOP (texture row 0 = frame
    // row 0), as two triangles.
    const float quad[] = {
        // x      y     u     v
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
    };
    gl_.GenVertexArrays(1, &vao_);
    gl_.BindVertexArray(vao_);
    gl_.GenBuffers(1, &vbo_);
    gl_.BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl_.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    gl_.EnableVertexAttribArray(0);
    gl_.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    gl_.EnableVertexAttribArray(1);
    gl_.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            reinterpret_cast<const void*>(2 * sizeof(float)));

    glGenTextures(1, &frame_tex_);
    glBindTexture(GL_TEXTURE_2D, frame_tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 320, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    // LINEAR is what makes the pixel-art shader's edge ramp work; the shader's UV
    // snapping keeps interiors exact.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GlPresenter::~GlPresenter() {
    if (context_) {
        SDL_GL_MakeCurrent(window_, context_);
        if (frame_tex_ != 0) {
            glDeleteTextures(1, &frame_tex_);
        }
        if (vbo_ != 0) {
            gl_.DeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0) {
            gl_.DeleteVertexArrays(1, &vao_);
        }
        if (program_ != 0) {
            gl_.DeleteProgram(program_);
        }
        SDL_GL_DestroyContext(context_);
    }
}

void GlPresenter::upload_frame(const IndexedFramebuffer& frame) {
    const auto rgba = frame.to_rgba();  // r|g<<8|b<<16|a<<24 == RGBA bytes (LE)
    glBindTexture(GL_TEXTURE_2D, frame_tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width(), frame.height(), GL_RGBA,
                    GL_UNSIGNED_BYTE, rgba.data());
}

void GlPresenter::draw_flat(const IndexedFramebuffer& frame, int target_w, int target_h,
                            int logical_h) {
    upload_frame(frame);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const Viewport vp = compute_letterbox_viewport(target_w, target_h, 320, logical_h);
    glViewport(vp.x, vp.y, vp.w, vp.h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    gl_.UseProgram(program_);
    gl_.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame_tex_);
    gl_.Uniform1i(u_tex_, 0);
    gl_.Uniform2f(u_tex_size_, static_cast<float>(frame.width()),
                  static_cast<float>(frame.height()));
    gl_.BindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void GlPresenter::present_flat(const IndexedFramebuffer& frame, int logical_h) {
    SDL_GL_MakeCurrent(window_, context_);
    gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    draw_flat(frame, w, h, logical_h);
    SDL_GL_SwapWindow(window_);
}

std::vector<std::uint8_t> GlPresenter::render_flat_offscreen(const IndexedFramebuffer& frame,
                                                             int w, int h, int logical_h) {
    SDL_GL_MakeCurrent(window_, context_);
    OffscreenTarget target = make_offscreen_target(gl_, w, h);
    gl_.BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    draw_flat(frame, w, h, logical_h);
    auto pixels = read_target_rgba(gl_, target);
    gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
    destroy_offscreen_target(gl_, target);
    return pixels;
}

}  // namespace bumpy
```

- [ ] **Step 3: CMake — add `gl_presenter.cpp` to `bumpy_platform_sdl3`. Build — expected: clean compile.**

- [ ] **Step 4: Commit.** `git commit -m "feat(gl3): GlPresenter -- flat 320x200 path with pixel-art shader"`

---

### Task 5: `--present-parity` — prove the GL flat path matches

**Files:**
- Modify: `src/app/main.cpp` (new tool + CLI branch)

**Interfaces:**
- Consumes: `GlPresenter`, `render_menu_frame` (already in main.cpp), `LevelResources`, `render_board`, `draw_bum_entities`, `WorldResources`, `render_map`, `WorldMap`.
- Produces: CLI `bumpy_port --present-parity` — exit 0 iff every case matches a CPU nearest-neighbour reference exactly at integer scales 2 and 4.

- [ ] **Step 1: Add the tool to `main.cpp`** (in the anonymous namespace, after `render_menu_frame`; include `"platform_gl3/gl_presenter.h"` and `"game/world_map.h"` is already included):

```cpp
// CPU nearest-neighbour reference upscale: each frame pixel becomes a k x k block.
std::vector<std::uint8_t> nearest_upscale_rgba(const bumpy::IndexedFramebuffer& frame, int k) {
    const auto src = frame.to_rgba();
    const int w = frame.width();
    const int h = frame.height();
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * k * h * k * 4);
    for (int y = 0; y < h * k; ++y) {
        for (int x = 0; x < w * k; ++x) {
            const std::uint32_t p = src[static_cast<std::size_t>(y / k) * w + (x / k)];
            const std::size_t o = (static_cast<std::size_t>(y) * w * k + x) * 4;
            out[o] = static_cast<std::uint8_t>(p & 0xff);
            out[o + 1] = static_cast<std::uint8_t>((p >> 8) & 0xff);
            out[o + 2] = static_cast<std::uint8_t>((p >> 16) & 0xff);
            out[o + 3] = static_cast<std::uint8_t>((p >> 24) & 0xff);
        }
    }
    return out;
}

// Renders three representative composed screens through the GL flat path at
// integer scales and compares them byte-for-byte with the CPU nearest reference.
// This is the spec's flat-path parity gate: interior pixels must be 1:1.
int present_parity(const std::filesystem::path& asset_root) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window =
        SDL_CreateWindow("parity", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    int failures = 0;
    {
        bumpy::GlPresenter presenter(window);

        std::vector<std::pair<std::string, bumpy::IndexedFramebuffer>> cases;
        cases.emplace_back("menu", render_menu_frame(asset_root, 0));

        const auto level = bumpy::LevelResources::load(asset_root, 1);
        const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
        bumpy::IndexedFramebuffer board(320, 200);
        bumpy::render_board(level, 0, {}, board);
        bumpy::draw_bum_entities(level.bum_entities(0), bank.bytes(), board);
        cases.emplace_back("board", std::move(board));

        auto world = bumpy::WorldResources::load(asset_root, 1);
        bumpy::WorldMap map(1);
        bumpy::IndexedFramebuffer mapframe(320, 200);
        bumpy::render_map(world.backdrop(), map.view(), bank.bytes(), mapframe);
        cases.emplace_back("map", std::move(mapframe));

        for (const auto& [name, frame] : cases) {
            for (const int k : {2, 4}) {
                const auto got = presenter.render_flat_offscreen(frame, 320 * k, 200 * k, 200);
                const auto want = nearest_upscale_rgba(frame, k);
                const bool pass = got == want;
                std::cout << name << " x" << k << ": " << (pass ? "PASS" : "FAIL") << '\n';
                if (!pass) {
                    ++failures;
                }
            }
        }
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}
```

And the CLI branch (before the `--start-world` block):

```cpp
        if (argc == 2 && std::string_view(argv[1]) == "--present-parity") {
            return present_parity(asset_root);
        }
```

- [ ] **Step 2: Build and run:** `build/windows-debug/Debug/bumpy_port.exe --present-parity` from the project root. Expected output — six `PASS` lines, exit 0. If any FAIL: the pixel-art shader's clamp must hit exact texel centers at saturation (`seam ± 0.5`); debug by writing both buffers to BMP and diffing.

- [ ] **Step 3: Commit.** `git commit -m "feat(gl3): --present-parity gate -- GL flat path matches CPU nearest at integer scales"`

---

### Task 6: Switch SdlApp to the GL presenter (SDL_Renderer kept as fallback)

**Files:**
- Modify: `src/platform_sdl3/sdl_app.h`, `src/platform_sdl3/sdl_app.cpp`

**Interfaces:**
- Consumes: `GlPresenter`.
- Produces: `SdlApp` members `std::unique_ptr<GlPresenter> gl_;` (null ⇒ fallback members `renderer_`/`texture_` are live). Behavior contract for later tasks: `bool SdlApp::gl_available() const { return gl_ != nullptr; }` (add to the header, public).

- [ ] **Step 1: Rework the constructor** in `sdl_app.cpp` (keep `require`, keep the existing PIXELART/logical-presentation comments with the fallback code):

```cpp
SdlApp::SdlApp() {
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO));
    // Preferred path: a GL 3.3 core context (the GlPresenter carries both the flat
    // and the 3D presentation). Attributes must be set before window creation.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    window_ = SDL_CreateWindow("Bumpy's Arcade Fantasy", 960, 600,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (window_) {
        try {
            gl_ = std::make_unique<GlPresenter>(window_);
        } catch (const std::exception& error) {
            std::cerr << "warning: OpenGL 3.3 unavailable, falling back to SDL_Renderer"
                         " (3D mode disabled): " << error.what() << '\n';
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
    }
    if (!gl_) {
        // Fallback: the original SDL_Renderer presentation, flat only.
        window_ = SDL_CreateWindow("Bumpy's Arcade Fantasy", 960, 600, SDL_WINDOW_RESIZABLE);
        if (!window_) { SDL_Quit(); throw std::runtime_error(SDL_GetError()); }
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) { SDL_DestroyWindow(window_); SDL_Quit(); throw std::runtime_error(SDL_GetError()); }
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING, 320, 200);
        if (!texture_) { SDL_DestroyRenderer(renderer_); SDL_DestroyWindow(window_); SDL_Quit(); throw std::runtime_error(SDL_GetError()); }
        require(SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_PIXELART));
        require(SDL_SetRenderLogicalPresentation(
            renderer_, 320, 200, SDL_LOGICAL_PRESENTATION_LETTERBOX));
    }
}
```

Header changes (`sdl_app.h`): add `#include "platform_gl3/gl_presenter.h"`, `#include <memory>`; members `std::unique_ptr<GlPresenter> gl_;` (before `window_` is fine — destruction order: `gl_` declared AFTER `window_` so it destroys BEFORE the window; declare `gl_` last); public `[[nodiscard]] bool gl_available() const noexcept { return gl_ != nullptr; }`. Destructor: guard `texture_`/`renderer_` (already non-null-safe: SDL destroy functions accept null — keep as-is), `gl_.reset()` happens automatically before `SDL_DestroyWindow` given member order; make that order explicit with a comment.

- [ ] **Step 2: Route presentation** — in `run()`:

```cpp
    auto present_frame = [&]() {
        if (gl_) {
            gl_->present_flat(frame, square_pixels ? 200 : 240);
            return;
        }
        const auto rgba = frame.to_rgba();
        require(SDL_UpdateTexture(
            texture_, nullptr, rgba.data(), frame.width() * sizeof(std::uint32_t)));
        require(SDL_RenderClear(renderer_));
        require(SDL_RenderTexture(renderer_, texture_, nullptr, nullptr));
        require(SDL_RenderPresent(renderer_));
    };
```

And `apply_aspect` becomes a no-op on GL (the logical height is passed per-present):

```cpp
    auto apply_aspect = [&]() {
        if (!renderer_) {
            return;  // GL path: present_flat picks 200/240 from square_pixels directly
        }
        require(SDL_SetRenderLogicalPresentation(
            renderer_, 320, square_pixels ? 200 : 240, SDL_LOGICAL_PRESENTATION_LETTERBOX));
    };
```

- [ ] **Step 3: Build, run the game manually.** Verify by eye against a pre-change build: splash → menu → map → level, Alt+A both aspects, Alt+Enter fullscreen, window resize. Motion (ball bounce) must look identical. Run `ctest --preset windows-debug` — all pass (no core changes).

- [ ] **Step 4: Commit.** `git commit -m "feat(sdl3): present through GlPresenter; SDL_Renderer kept as no-GL fallback"`

---

### Task 7: `bumpy_port.cfg` + `--render3d` + Alt+3 mode state

Alt+3 gets its full visible effect in Task 13; this task lands the complete switching/persistence plumbing with the mode flag routed into `SdlApp::run`.

**Files:**
- Modify: `src/app/main.cpp`, `src/platform_sdl3/sdl_app.h`, `src/platform_sdl3/sdl_app.cpp`

**Interfaces:**
- Consumes: `PortConfig`, `load_port_config`, `save_port_config`.
- Produces: `SdlApp::run(..., PortConfig config, std::filesystem::path config_path)` — two appended parameters. Inside `run`, local state `bool render3d` (used by Task 13); every Alt+3 / Alt+A / Alt+Enter updates `config` and best-effort-saves it.

- [ ] **Step 1: main.cpp — config path helper + load + flag.** Add near `find_asset_root`:

```cpp
// The config sits next to the exe ("the port's first on-disk persistence"); if the
// exe path is unusable, fall back to the current directory.
std::filesystem::path config_file_path(std::string_view executable_path) {
    std::error_code error;
    auto p = std::filesystem::weakly_canonical(std::filesystem::path(executable_path), error);
    if (!error && p.has_parent_path()) {
        return p.parent_path() / "bumpy_port.cfg";
    }
    return std::filesystem::current_path() / "bumpy_port.cfg";
}
```

In `main`: after computing `asset_root`, add

```cpp
        const auto cfg_path =
            config_file_path(argc > 0 ? std::string_view(argv[0]) : std::string_view{});
        bumpy::PortConfig config = bumpy::load_port_config(cfg_path);
        // --render3d anywhere on the command line: start in 3D (overrides the config).
        bool render3d_flag = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--render3d") {
                render3d_flag = true;
            }
        }
        if (render3d_flag) {
            config.render3d = true;
        }
```

`run_sdl_menu` signature gains `bumpy::PortConfig config, const std::filesystem::path& cfg_path` and forwards both to `sdl.run(...)`. The `--start-world` branch and the default branch pass them. The arg-count checks: `--start-world N` currently requires `argc == 3`; change the flag scan to *remove* `--render3d` from consideration by counting: simplest — collect non-flag args into a `std::vector<std::string_view> args` first and run the existing checks against that vector's size/contents. Show: 

```cpp
        std::vector<std::string_view> args;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) != "--render3d") {
                args.emplace_back(argv[i]);
            }
        }
```

…and replace `argc`/`argv[i]` in the `--start-world`/usage/default branches with `args.size() + 1` / `args[i-1]` equivalents (only those three sites; the `--render-*`/`--dump-*` tool branches above keep raw `argc/argv` — mixing `--render3d` with dump tools is unsupported and falls through to usage).

- [ ] **Step 2: SdlApp — accept + apply + persist.** `sdl_app.h`: `#include "core/port_config.h"`, `run(..., PortConfig config, std::filesystem::path config_path)`. In `run()`:

```cpp
    // Presentation state, seeded from the persisted config. render3d only arms when
    // the GL presenter is live (Alt+3 needs shaders); the flag itself is kept so a
    // machine upgrade re-enables it.
    bool square_pixels = config.square_pixels;
    bool render3d = config.render3d && gl_ != nullptr;
    apply_aspect();
    if (config.fullscreen) {
        SDL_SetWindowFullscreen(window_, true);
    }
    auto persist = [&]() {
        if (!save_port_config(config_path, config)) {
            std::cerr << "warning: could not write " << config_path.string() << '\n';
        }
    };
```

(The existing `bool square_pixels = true;` declaration is replaced by this block.) Event handlers:

```cpp
                if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
                    (event.key.mod & SDL_KMOD_ALT)) {
                    const bool fullscreen =
                        (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(window_, !fullscreen);
                    config.fullscreen = !fullscreen;
                    persist();
                } else if (event.key.key == SDLK_A && (event.key.mod & SDL_KMOD_ALT)) {
                    square_pixels = !square_pixels;
                    apply_aspect();
                    config.square_pixels = square_pixels;
                    persist();
                } else if (event.key.key == SDLK_3 && (event.key.mod & SDL_KMOD_ALT)) {
                    // Alt+3: original <-> 3D diorama (hard cut, per the design spec).
                    if (gl_) {
                        render3d = !render3d;
                        config.render3d = render3d;
                        persist();
                    } else {
                        std::cerr << "3D mode unavailable: no OpenGL 3.3\n";
                    }
                } else {
```

- [ ] **Step 3: Build; manual test.** Run, toggle Alt+A + Alt+Enter, quit; verify `bumpy_port.cfg` appears next to the exe with the chosen values; relaunch → settings restored; `--render3d` sets `render3d=1` on next save. `ctest` still green.

- [ ] **Step 4: Commit.** `git commit -m "feat(app): bumpy_port.cfg persistence, --render3d flag, Alt+3 mode state"`

---

### Task 8: `for_each_entity_sprite` — one source of truth for entity placement

**Files:**
- Modify: `src/video/board_renderer.h`, `src/video/board_renderer.cpp`
- Test: `tests/cpp/board_renderer_test.cpp` (append)

**Interfaces:**
- Produces (namespace `bumpy`, in `board_renderer.h`):
  - `enum class EntityLayer { a, b, c };`
  - `void for_each_entity_sprite(const BumEntities& bum, const std::function<void(EntityLayer layer, int frame_index, int x, int y)>& fn);` — faithful order (row-major; per cell A, then B — never col 7 —, then C), positions are the exact blit top-lefts (A: `entity_layer_ab_position + y_offset`; B: `entity_layer_b_position + y_offset`; C: `bum_cell_position`).
- Consumed by `draw_bum_entities` (rewired) and Task 10's `build_live_quads`.

- [ ] **Step 1: Write the failing test** (append to `tests/cpp/board_renderer_test.cpp`; it already includes the needed headers — add `#include <tuple>`, `#include <vector>` and `#include "resources/entity_sprites.h"` if missing):

```cpp
TEST_CASE("for_each_entity_sprite yields faithful frames and positions") {
    bumpy::BumEntities bum{};
    bum.bytes[0 * 8 + 2] = 1;                                       // layer A, col 2 row 0: peg
    bum.bytes[bumpy::BumEntities::layer_b_offset + 1 * 8 + 7] = 1;  // layer B col 7: never drawn
    bum.bytes[0x60 + 3 * 8 + 5] = 0x1b;                             // layer C, col 5 row 3

    std::vector<std::tuple<bumpy::EntityLayer, int, int, int>> seen;
    bumpy::for_each_entity_sprite(bum, [&](bumpy::EntityLayer layer, int frame, int x, int y) {
        seen.emplace_back(layer, frame, x, y);
    });

    REQUIRE(seen.size() == 2);
    // Layer A code 1 -> frame 0x40, y_offset 5 at DS:0xf4 slot (col*40, 24+row*32).
    REQUIRE(std::get<0>(seen[0]) == bumpy::EntityLayer::a);
    REQUIRE(std::get<1>(seen[0]) == 0x40);
    REQUIRE(std::get<2>(seen[0]) == 80);
    REQUIRE(std::get<3>(seen[0]) == 24 + 5);
    // Layer C value 0x1b -> frame 0x194 at the DS:0x274 cell position.
    const auto cpos = bumpy::bum_cell_position(5, 3);
    REQUIRE(std::get<0>(seen[1]) == bumpy::EntityLayer::c);
    REQUIRE(std::get<1>(seen[1]) == 0x194);
    REQUIRE(std::get<2>(seen[1]) == cpos.x);
    REQUIRE(std::get<3>(seen[1]) == cpos.y);
}
```

- [ ] **Step 2: Run — fails to compile (`for_each_entity_sprite` undefined).**

- [ ] **Step 3: Implement.** `board_renderer.h` — add after `EntitySpriteStats`/before `draw_bum_entities` (plus `#include <functional>`):

```cpp
enum class EntityLayer { a, b, c };

// Enumerate the BUM grid's entity sprites in the faithful draw order of
// FUN_1000_2a78 (row-major; per cell layer A, then B -- never col 7 --, then C),
// yielding each sprite's bank frame index and exact blit top-left. The single
// source of entity placement, shared by draw_bum_entities (flat path) and the
// 3D scene builder, so both compose identically by construction.
void for_each_entity_sprite(
    const BumEntities& bum,
    const std::function<void(EntityLayer layer, int frame_index, int x, int y)>& fn);
```

`board_renderer.cpp` — implement and rewire `draw_bum_entities` over it:

```cpp
void for_each_entity_sprite(
    const BumEntities& bum,
    const std::function<void(EntityLayer, int, int, int)>& fn) {
    for (int row = 0; row < BumEntities::rows; ++row) {
        for (int col = 0; col < BumEntities::columns; ++col) {
            if (const auto a = entity_layer_a_sprite(bum.layer_a(col, row)); a.present()) {
                const auto pos = entity_layer_ab_position(col, row);
                fn(EntityLayer::a, a.frame_index, pos.x, pos.y + a.y_offset);
            }
            if (const std::uint8_t bv = bum.layer_b(col, row); bv != 0 && col != 7) {
                if (const auto b = entity_layer_b_sprite(bv); b.present()) {
                    const auto pos = entity_layer_b_position(col, row);
                    fn(EntityLayer::b, b.frame_index, pos.x, pos.y + b.y_offset);
                }
            }
            if (const std::uint8_t cv = bum.layer_c(col, row); cv != 0) {
                const auto pos = bum_cell_position(col, row);
                fn(EntityLayer::c, entity_layer_c_frame(cv), pos.x, pos.y);
            }
        }
    }
}

EntitySpriteStats draw_bum_entities(const BumEntities& bum,
                                    std::span<const std::uint8_t> sprite_bank,
                                    IndexedFramebuffer& target) {
    EntitySpriteStats stats;
    for_each_entity_sprite(bum, [&](EntityLayer layer, int frame, int x, int y) {
        const bool drawn = blit_bank_frame(sprite_bank, frame, x, y, target);
        if (!drawn) {
            ++stats.skipped;
        } else if (layer == EntityLayer::a) {
            ++stats.layer_a;
        } else if (layer == EntityLayer::b) {
            ++stats.layer_b;
        } else {
            ++stats.layer_c;
        }
    });
    return stats;
}
```

(`blit_bank_frame` must move above `for_each_entity_sprite`'s new use site or stay in its anonymous namespace above `draw_bum_entities` — it already is; keep the enumeration function in the public namespace right before `draw_bum_entities`.)

- [ ] **Step 4: Build + run tests — the new test AND all existing board_renderer/entity tests pass** (this refactor must not change a single composed pixel; `--render-board ... sprites` output can be diffed as extra proof).

- [ ] **Step 5: Commit.** `git commit -m "refactor(video): extract for_each_entity_sprite as the shared entity-placement source"`

---

### Task 9: mat4 + gaussian blur (CPU, unit-tested)

**Files:**
- Create: `src/video3d/mat4.h`, `src/video3d/mat4.cpp`, `src/video3d/blur.h`, `src/video3d/blur.cpp`
- Modify: `CMakeLists.txt` (both `.cpp` → `bumpy_core`; `tests/cpp/mat4_test.cpp`, `tests/cpp/blur_test.cpp` → `bumpy_tests`)
- Test: `tests/cpp/mat4_test.cpp`, `tests/cpp/blur_test.cpp`

**Interfaces:**
- Produces (namespace `bumpy`):
  - `struct Mat4 { std::array<float, 16> m; }` — column-major (feeds `glUniformMatrix4fv(..., GL_FALSE, ...)`)
  - `Mat4 mat4_identity()`, `Mat4 mat4_multiply(const Mat4& a, const Mat4& b)`, `Mat4 mat4_translate(float x, float y, float z)`, `Mat4 mat4_perspective(float fovy_rad, float aspect, float znear, float zfar)` (GL convention, looks down −z)
  - `struct Vec4 { float x, y, z, w; }`, `Vec4 mat4_transform(const Mat4& m, const Vec4& v)`
  - `void gaussian_blur_rgba(std::vector<std::uint8_t>& rgba, int w, int h, float sigma)` — separable, clamped edges, `sigma <= 0` no-op
  - `void gaussian_blur_alpha(std::vector<std::uint8_t>& alpha, int w, int h, float sigma)` — single-channel variant (shadow textures, Task 14)

- [ ] **Step 1: Write the failing tests.** `tests/cpp/mat4_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "video3d/mat4.h"

#include <cmath>

using bumpy::Mat4;
using bumpy::Vec4;

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}  // namespace

TEST_CASE("identity transform leaves a point unchanged") {
    const Vec4 v = bumpy::mat4_transform(bumpy::mat4_identity(), {3.0f, -2.0f, 5.0f, 1.0f});
    REQUIRE(near(v.x, 3.0f));
    REQUIRE(near(v.y, -2.0f));
    REQUIRE(near(v.z, 5.0f));
    REQUIRE(near(v.w, 1.0f));
}

TEST_CASE("translate moves a point") {
    const Vec4 v =
        bumpy::mat4_transform(bumpy::mat4_translate(10.0f, 20.0f, 30.0f), {1.0f, 2.0f, 3.0f, 1.0f});
    REQUIRE(near(v.x, 11.0f));
    REQUIRE(near(v.y, 22.0f));
    REQUIRE(near(v.z, 33.0f));
}

TEST_CASE("multiply composes right-to-left") {
    const Mat4 t = bumpy::mat4_multiply(bumpy::mat4_translate(1.0f, 0.0f, 0.0f),
                                        bumpy::mat4_translate(0.0f, 2.0f, 0.0f));
    const Vec4 v = bumpy::mat4_transform(t, {0.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(near(v.x, 1.0f));
    REQUIRE(near(v.y, 2.0f));
}

TEST_CASE("perspective projects the frustum edge to NDC +-1") {
    // fovy 90 deg, aspect 1: a point at distance d with |y| = d sits on the frustum edge.
    const Mat4 p = bumpy::mat4_perspective(3.14159265f / 2.0f, 1.0f, 1.0f, 100.0f);
    const Vec4 top = bumpy::mat4_transform(p, {0.0f, 10.0f, -10.0f, 1.0f});
    REQUIRE(near(top.y / top.w, 1.0f));
    const Vec4 centre = bumpy::mat4_transform(p, {0.0f, 0.0f, -10.0f, 1.0f});
    REQUIRE(near(centre.x / centre.w, 0.0f));
    REQUIRE(near(centre.y / centre.w, 0.0f));
}
```

`tests/cpp/blur_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "video3d/blur.h"

#include <cstdint>
#include <numeric>
#include <vector>

TEST_CASE("sigma <= 0 is a no-op") {
    std::vector<std::uint8_t> img(5 * 5 * 4, 0);
    img[(2 * 5 + 2) * 4] = 200;
    auto copy = img;
    bumpy::gaussian_blur_rgba(img, 5, 5, 0.0f);
    REQUIRE(img == copy);
}

TEST_CASE("an impulse spreads symmetrically and keeps its energy") {
    const int w = 9;
    const int h = 9;
    std::vector<std::uint8_t> a(static_cast<std::size_t>(w) * h, 0);
    a[4 * w + 4] = 255;
    bumpy::gaussian_blur_alpha(a, w, h, 1.2f);
    REQUIRE(a[4 * w + 4] > a[4 * w + 5]);          // peak at centre
    REQUIRE(a[4 * w + 3] == a[4 * w + 5]);         // horizontal symmetry
    REQUIRE(a[3 * w + 4] == a[5 * w + 4]);         // vertical symmetry
    REQUIRE(a[4 * w + 5] > 0);                     // actually spread
    const int sum = std::accumulate(a.begin(), a.end(), 0);
    REQUIRE(sum >= 240);                           // energy preserved up to rounding
    REQUIRE(sum <= 270);
}

TEST_CASE("rgba blur touches colour channels independently and clamps edges") {
    const int w = 4;
    const int h = 1;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4, 0);
    img[0 * 4 + 0] = 255;  // red at x=0 (edge)
    bumpy::gaussian_blur_rgba(img, w, h, 1.0f);
    REQUIRE(img[0 * 4 + 0] > img[1 * 4 + 0]);  // red spreads right
    REQUIRE(img[1 * 4 + 1] == 0);              // green untouched
}
```

- [ ] **Step 2: Register in CMake, verify compile failure.**

- [ ] **Step 3: Implement.** `src/video3d/mat4.h`:

```cpp
#pragma once

#include <array>

namespace bumpy {

// Column-major 4x4 (m[col*4 + row]) -- feeds glUniformMatrix4fv(transpose=GL_FALSE).
struct Mat4 {
    std::array<float, 16> m{};
};

struct Vec4 {
    float x{};
    float y{};
    float z{};
    float w{};
};

[[nodiscard]] Mat4 mat4_identity();
[[nodiscard]] Mat4 mat4_multiply(const Mat4& a, const Mat4& b);  // a * b
[[nodiscard]] Mat4 mat4_translate(float x, float y, float z);
// Standard GL perspective: camera at origin looking down -z, y up.
[[nodiscard]] Mat4 mat4_perspective(float fovy_rad, float aspect, float znear, float zfar);
[[nodiscard]] Vec4 mat4_transform(const Mat4& m, const Vec4& v);

}  // namespace bumpy
```

`src/video3d/mat4.cpp`:

```cpp
#include "video3d/mat4.h"

#include <cmath>

namespace bumpy {

Mat4 mat4_identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 mat4_multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

Mat4 mat4_translate(float x, float y, float z) {
    Mat4 r = mat4_identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4 mat4_perspective(float fovy_rad, float aspect, float znear, float zfar) {
    const float f = 1.0f / std::tan(fovy_rad / 2.0f);
    Mat4 r;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

Vec4 mat4_transform(const Mat4& m, const Vec4& v) {
    return {
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w,
        m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w,
    };
}

}  // namespace bumpy
```

`src/video3d/blur.h`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace bumpy {

// Separable gaussian blur, clamped edges, radius = ceil(3*sigma). sigma <= 0: no-op.
// rgba: w*h*4 bytes, rows top-to-bottom (each channel blurred independently).
void gaussian_blur_rgba(std::vector<std::uint8_t>& rgba, int w, int h, float sigma);
// Single-channel variant (w*h bytes) -- used for baked sprite shadow silhouettes.
void gaussian_blur_alpha(std::vector<std::uint8_t>& alpha, int w, int h, float sigma);

}  // namespace bumpy
```

`src/video3d/blur.cpp`:

```cpp
#include "video3d/blur.h"

#include <algorithm>
#include <cmath>

namespace bumpy {

namespace {

std::vector<float> gaussian_kernel(float sigma) {
    const int radius = static_cast<int>(std::ceil(3.0f * sigma));
    std::vector<float> k(static_cast<std::size_t>(2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float v = std::exp(-(static_cast<float>(i) * i) / (2.0f * sigma * sigma));
        k[static_cast<std::size_t>(i + radius)] = v;
        sum += v;
    }
    for (auto& v : k) {
        v /= sum;
    }
    return k;
}

// One separable pass over `src` (stride `channels`, channel `c`), writing `dst`.
void blur_axis(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& dst, int w,
               int h, int channels, int c, const std::vector<float>& kernel, bool horizontal) {
    const int radius = static_cast<int>(kernel.size() / 2);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                const int sx = horizontal ? std::clamp(x + i, 0, w - 1) : x;
                const int sy = horizontal ? y : std::clamp(y + i, 0, h - 1);
                acc += kernel[static_cast<std::size_t>(i + radius)] *
                       src[(static_cast<std::size_t>(sy) * w + sx) * channels + c];
            }
            dst[(static_cast<std::size_t>(y) * w + x) * channels + c] =
                static_cast<std::uint8_t>(std::lround(std::clamp(acc, 0.0f, 255.0f)));
        }
    }
}

void blur(std::vector<std::uint8_t>& pixels, int w, int h, int channels, float sigma) {
    if (sigma <= 0.0f || w <= 0 || h <= 0) {
        return;
    }
    const auto kernel = gaussian_kernel(sigma);
    std::vector<std::uint8_t> tmp(pixels.size());
    for (int c = 0; c < channels; ++c) {
        blur_axis(pixels, tmp, w, h, channels, c, kernel, /*horizontal=*/true);
    }
    for (int c = 0; c < channels; ++c) {
        blur_axis(tmp, pixels, w, h, channels, c, kernel, /*horizontal=*/false);
    }
}

}  // namespace

void gaussian_blur_rgba(std::vector<std::uint8_t>& rgba, int w, int h, float sigma) {
    blur(rgba, w, h, 4, sigma);
}

void gaussian_blur_alpha(std::vector<std::uint8_t>& alpha, int w, int h, float sigma) {
    blur(alpha, w, h, 1, sigma);
}

}  // namespace bumpy
```

- [ ] **Step 4: Build + tests PASS.**

- [ ] **Step 5: Commit.** `git commit -m "feat(video3d): column-major mat4 + separable gaussian blur"`

---

### Task 10: Scene3d — CPU scene model and builders

**Files:**
- Create: `src/video3d/scene3d.h`, `src/video3d/scene3d.cpp`
- Modify: `CMakeLists.txt` (`scene3d.cpp` → `bumpy_core`; `tests/cpp/scene3d_test.cpp` → `bumpy_tests`)
- Test: `tests/cpp/scene3d_test.cpp`

**Interfaces:**
- Consumes: `for_each_entity_sprite` (Task 8), `gaussian_blur_rgba` (Task 9), `render_board`, `decode_sprite_frame`, `MenuImage`, `entity_layer_b_position`/`entity_layer_ab_position`, `ObjectAnimSprite`/`kAnimHiddenFrame`.
- Produces (namespace `bumpy`) — everything below; consumed by Tasks 11, 12, 13:

- [ ] **Step 1: Write `src/video3d/scene3d.h`:**

```cpp
#pragma once

#include "core/indexed_framebuffer.h"
#include "game/object_anim.h"
#include "resources/level_resources.h"
#include "video/menu_renderer.h"  // MenuImage

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace bumpy {

// --- Diorama geometry. World space = board pixels (x right, y DOWN, z toward the
// viewer); conversion to GL axes (y up, origin at board centre) happens when faces
// are emitted (slab_mesh) and when uniforms are set. All values are tuning knobs.
inline constexpr float kWallZ = -28.0f;       // back wall (the DEC mural)
inline constexpr float kSlabDepth = 5.0f;     // extrusion thickness of platforms
inline constexpr float kBillboardAbZ = kSlabDepth;         // irregular A/B sprites
inline constexpr float kCollectibleZ = kSlabDepth + 2.0f;  // plane C
inline constexpr float kActorZ = kSlabDepth + 4.0f;        // ball and monster
inline constexpr float kCameraFovYDeg = 26.0f;
inline constexpr float kParallaxGain = 0.05f;  // camera shift per ball offset px
inline constexpr float kParallaxEase = 0.12f;  // easing per presented frame
inline constexpr float kWallBlurSigma = 1.6f;  // baked mural DOF

// Camera distance that makes the z=0 plane subtend exactly 200 world px of height.
[[nodiscard]] float scene_camera_distance();

enum class QuadKind { slab, billboard };

// One art element in the scene: the frame's blit rect (origin already subtracted
// for the ball/monster) on the z=0-anchored stage.
struct SceneQuad {
    int frame_index{};
    float x{};
    float y{};  // top-left of the blit in board space
    int w{};
    int h{};
    QuadKind kind{QuadKind::billboard};
    float z{};  // front plane (slab front face / billboard plane)
};

// Opaque bounds of a decoded frame (transparent = sprite_transparent_index).
struct OpaqueBounds {
    int x{};
    int y{};
    int w{};
    int h{};
    bool any{};
};
[[nodiscard]] OpaqueBounds opaque_bounds(const MenuImage& image);

// slab <=> the opaque silhouette is a completely filled rectangle (lane bars,
// blocks). Spiky/irregular art stays a crisp billboard -- extruding it would smear.
[[nodiscard]] QuadKind classify_sprite(const MenuImage& image);

// Decoded-frame + classification cache over the BUMSPJEU bank.
class SpriteCache {
public:
    explicit SpriteCache(std::span<const std::uint8_t> bank) : bank_(bank) {}
    // nullptr when the frame does not decode (skipped, like blit_bank_frame).
    const MenuImage* frame(int frame_index);
    QuadKind kind(int frame_index);  // billboard for undecodable frames
private:
    std::span<const std::uint8_t> bank_;
    std::unordered_map<int, std::optional<MenuImage>> frames_;
    std::unordered_map<int, QuadKind> kinds_;
};

// Static per-board scene part: the wall (render_board output with the baked DOF
// blur) and the board palette that sprite textures are built with.
struct Scene3d {
    std::vector<std::uint8_t> wall_rgba;  // 320*200*4, rows top-to-bottom, pre-blurred
    std::array<Rgba, 256> palette{};
};
[[nodiscard]] Scene3d build_scene3d(const LevelResources& level, std::size_t board_index,
                                    std::span<const std::uint8_t> backdrop_screen);

struct BallPose {
    int frame{};
    int x{};
    int y{};
};
struct MonsterPose {
    int frame{};
    int x{};
    int y{};
};

// The frame's live quads, mirroring the flat render_level composition exactly:
// entities (anim cells pre-blanked by the caller, same as the flat path), spring
// anims, monster, ball (hidden sentinel frame 100 skipped, undecodable skipped).
[[nodiscard]] std::vector<SceneQuad> build_live_quads(const BumEntities& entities,
                                                      std::span<const ObjectAnimSprite> anims,
                                                      const std::optional<MonsterPose>& monster,
                                                      const BallPose& ball,
                                                      SpriteCache& sprites);

}  // namespace bumpy
```

- [ ] **Step 2: Write the failing tests** — `tests/cpp/scene3d_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "resources/entity_sprites.h"
#include "resources/menu_resources.h"
#include "resources/sprite_frame.h"
#include "video3d/scene3d.h"

#include <filesystem>

namespace {

const std::filesystem::path root = ".";  // tests run from the project root

// A synthetic frame: w x h, all transparent except a filled rect.
bumpy::MenuImage make_frame(int w, int h, int rx, int ry, int rw, int rh) {
    bumpy::MenuImage img;
    img.width = w;
    img.height = h;
    img.pixels.assign(static_cast<std::size_t>(w) * h, bumpy::sprite_transparent_index);
    for (int y = ry; y < ry + rh; ++y) {
        for (int x = rx; x < rx + rw; ++x) {
            img.pixels[static_cast<std::size_t>(y) * w + x] = 3;
        }
    }
    return img;
}

}  // namespace

TEST_CASE("opaque_bounds finds the tight opaque rect") {
    const auto img = make_frame(10, 6, 2, 1, 5, 3);
    const auto b = bumpy::opaque_bounds(img);
    REQUIRE(b.any);
    REQUIRE(b.x == 2);
    REQUIRE(b.y == 1);
    REQUIRE(b.w == 5);
    REQUIRE(b.h == 3);
}

TEST_CASE("solid rectangle classifies as slab, irregular as billboard, empty as billboard") {
    REQUIRE(bumpy::classify_sprite(make_frame(10, 6, 2, 1, 5, 3)) == bumpy::QuadKind::slab);

    auto spiky = make_frame(10, 6, 2, 1, 5, 3);
    spiky.pixels[1 * 10 + 4] = bumpy::sprite_transparent_index;  // poke a hole
    REQUIRE(bumpy::classify_sprite(spiky) == bumpy::QuadKind::billboard);

    bumpy::MenuImage empty;
    empty.width = 4;
    empty.height = 4;
    empty.pixels.assign(16, bumpy::sprite_transparent_index);
    REQUIRE(bumpy::classify_sprite(empty) == bumpy::QuadKind::billboard);
}

TEST_CASE("build_live_quads mirrors the flat composition") {
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");
    bumpy::SpriteCache sprites(bank.bytes());

    bumpy::BumEntities entities{};
    entities.bytes[0 * 8 + 2] = 1;  // layer A peg at col 2 row 0 (frame 0x40, y_offset 5)

    // One spring anim on a hidden step and the ball on the hidden sentinel: neither
    // may produce a quad.
    const bumpy::ObjectAnimSprite hidden{
        /*cell=*/3, /*layer_b=*/false, /*frame_index=*/bumpy::kAnimHiddenFrame, /*y_offset=*/0};
    const auto quads = bumpy::build_live_quads(
        entities, {&hidden, 1}, std::nullopt, bumpy::BallPose{100, 87, 100}, sprites);

    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].frame_index == 0x40);
    REQUIRE(quads[0].x == 80.0f);
    REQUIRE(quads[0].y == 29.0f);  // 24 + y_offset 5
    const auto* img = sprites.frame(0x40);
    REQUIRE(img != nullptr);
    REQUIRE(quads[0].w == img->width);
    REQUIRE(quads[0].h == img->height);
}

TEST_CASE("ball and monster quads subtract the frame origin like draw_ball/draw_monster") {
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");
    bumpy::SpriteCache sprites(bank.bytes());
    const bumpy::BumEntities entities{};

    // Any decodable bank frame works here (build_live_quads only needs size/origin);
    // 0x40 is the layer-A peg frame the entity tests already rely on.
    const int ball_frame = 0x40;
    const auto* img = sprites.frame(ball_frame);
    REQUIRE(img != nullptr);

    const auto quads = bumpy::build_live_quads(entities, {}, std::nullopt,
                                               bumpy::BallPose{ball_frame, 87, 111}, sprites);
    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].x == static_cast<float>(87 - img->origin_x));
    REQUIRE(quads[0].y == static_cast<float>(111 - img->origin_y));
    REQUIRE(quads[0].kind == bumpy::QuadKind::billboard);
    REQUIRE(quads[0].z == bumpy::kActorZ);
}

TEST_CASE("collectibles are billboards on the collectible plane") {
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");
    bumpy::SpriteCache sprites(bank.bytes());

    bumpy::BumEntities entities{};
    entities.bytes[0x60 + 3 * 8 + 5] = 0x1b;  // layer C

    const auto quads =
        bumpy::build_live_quads(entities, {}, std::nullopt, bumpy::BallPose{100, 0, 0}, sprites);
    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].kind == bumpy::QuadKind::billboard);
    REQUIRE(quads[0].z == bumpy::kCollectibleZ);
}

TEST_CASE("build_scene3d bakes a blurred 320x200 wall with the board palette") {
    const auto level = bumpy::LevelResources::load(root, 1);
    const auto scene = bumpy::build_scene3d(level, 0, {});
    REQUIRE(scene.wall_rgba.size() == 320u * 200u * 4u);
    // Palette entry 0 is the board's own colour 0 (opaque).
    REQUIRE(scene.palette[0].a == 0xff);
}
```

(If `ObjectAnimSprite`'s field order differs, initialize it with designated initializers matching `src/game/object_anim.h`.)

- [ ] **Step 3: Register in CMake; verify compile failure.**

- [ ] **Step 4: Implement `src/video3d/scene3d.cpp`:**

```cpp
#include "video3d/scene3d.h"

#include "resources/entity_sprites.h"
#include "resources/sprite_frame.h"
#include "video/board_renderer.h"
#include "video3d/blur.h"

#include <cmath>
#include <exception>
#include <numbers>

namespace bumpy {

float scene_camera_distance() {
    const float half_fov = kCameraFovYDeg * std::numbers::pi_v<float> / 360.0f;
    return 100.0f / std::tan(half_fov);
}

OpaqueBounds opaque_bounds(const MenuImage& image) {
    OpaqueBounds b;
    int min_x = image.width;
    int min_y = image.height;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (image.pixels[static_cast<std::size_t>(y) * image.width + x] !=
                sprite_transparent_index) {
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }
        }
    }
    if (max_x < 0) {
        return b;
    }
    b.any = true;
    b.x = min_x;
    b.y = min_y;
    b.w = max_x - min_x + 1;
    b.h = max_y - min_y + 1;
    return b;
}

QuadKind classify_sprite(const MenuImage& image) {
    const OpaqueBounds b = opaque_bounds(image);
    if (!b.any) {
        return QuadKind::billboard;
    }
    for (int y = b.y; y < b.y + b.h; ++y) {
        for (int x = b.x; x < b.x + b.w; ++x) {
            if (image.pixels[static_cast<std::size_t>(y) * image.width + x] ==
                sprite_transparent_index) {
                return QuadKind::billboard;
            }
        }
    }
    return QuadKind::slab;
}

const MenuImage* SpriteCache::frame(int frame_index) {
    auto it = frames_.find(frame_index);
    if (it == frames_.end()) {
        std::optional<MenuImage> decoded;
        try {
            decoded = decode_sprite_frame(bank_, frame_index);
        } catch (const std::exception&) {
            // undecodable -> cached nullopt, skipped exactly like blit_bank_frame
        }
        it = frames_.emplace(frame_index, std::move(decoded)).first;
    }
    return it->second.has_value() ? &*it->second : nullptr;
}

QuadKind SpriteCache::kind(int frame_index) {
    auto it = kinds_.find(frame_index);
    if (it == kinds_.end()) {
        const MenuImage* img = frame(frame_index);
        it = kinds_.emplace(frame_index, img ? classify_sprite(*img) : QuadKind::billboard)
                 .first;
    }
    return it->second;
}

Scene3d build_scene3d(const LevelResources& level, std::size_t board_index,
                      std::span<const std::uint8_t> backdrop_screen) {
    IndexedFramebuffer frame(320, 200);
    render_board(level, board_index, backdrop_screen, frame);

    Scene3d scene;
    scene.palette = frame.palette();
    scene.wall_rgba.resize(static_cast<std::size_t>(320) * 200 * 4);
    const auto pixels = frame.pixels();
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const Rgba c = scene.palette[pixels[i]];
        scene.wall_rgba[i * 4] = c.r;
        scene.wall_rgba[i * 4 + 1] = c.g;
        scene.wall_rgba[i * 4 + 2] = c.b;
        scene.wall_rgba[i * 4 + 3] = 0xff;
    }
    gaussian_blur_rgba(scene.wall_rgba, 320, 200, kWallBlurSigma);
    return scene;
}

namespace {

void push_entity_or_anim_quad(std::vector<SceneQuad>& out, SpriteCache& sprites,
                              EntityLayer layer, int frame, float x, float y) {
    const MenuImage* img = sprites.frame(frame);
    if (!img) {
        return;
    }
    SceneQuad q;
    q.frame_index = frame;
    q.x = x;
    q.y = y;
    q.w = img->width;
    q.h = img->height;
    if (layer == EntityLayer::c) {
        q.kind = QuadKind::billboard;
        q.z = kCollectibleZ;
    } else if (sprites.kind(frame) == QuadKind::slab) {
        q.kind = QuadKind::slab;
        q.z = kSlabDepth;
    } else {
        q.kind = QuadKind::billboard;
        q.z = kBillboardAbZ;
    }
    out.push_back(q);
}

}  // namespace

std::vector<SceneQuad> build_live_quads(const BumEntities& entities,
                                        std::span<const ObjectAnimSprite> anims,
                                        const std::optional<MonsterPose>& monster,
                                        const BallPose& ball, SpriteCache& sprites) {
    std::vector<SceneQuad> out;
    for_each_entity_sprite(entities, [&](EntityLayer layer, int frame, int x, int y) {
        push_entity_or_anim_quad(out, sprites, layer, frame, static_cast<float>(x),
                                 static_cast<float>(y));
    });
    for (const auto& a : anims) {
        if (a.frame_index == kAnimHiddenFrame) {
            continue;  // blink-off step draws nothing, like draw_object_anims
        }
        const int col = static_cast<int>(a.cell % 8);
        const int row = static_cast<int>(a.cell / 8);
        const auto pos = a.layer_b ? entity_layer_b_position(col, row)
                                   : entity_layer_ab_position(col, row);
        push_entity_or_anim_quad(out, sprites,
                                 a.layer_b ? EntityLayer::b : EntityLayer::a,
                                 static_cast<int>(a.frame_index), static_cast<float>(pos.x),
                                 static_cast<float>(pos.y + a.y_offset));
    }
    if (monster) {
        if (const MenuImage* img = sprites.frame(monster->frame)) {
            out.push_back({monster->frame, static_cast<float>(monster->x - img->origin_x),
                           static_cast<float>(monster->y - img->origin_y), img->width,
                           img->height, QuadKind::billboard, kActorZ});
        }
    }
    if (ball.frame != 100) {  // hidden sentinel: FUN_1000_1cb2 skips it
        if (const MenuImage* img = sprites.frame(ball.frame)) {
            out.push_back({ball.frame, static_cast<float>(ball.x - img->origin_x),
                           static_cast<float>(ball.y - img->origin_y), img->width,
                           img->height, QuadKind::billboard, kActorZ});
        }
    }
    return out;
}

}  // namespace bumpy
```

- [ ] **Step 5: Build + tests PASS.**

- [ ] **Step 6: Commit.** `git commit -m "feat(video3d): Scene3d -- wall bake, sprite classification, live-quad builder"`

---

### Task 11: Slab/billboard face geometry

**Files:**
- Create: `src/video3d/slab_mesh.h`, `src/video3d/slab_mesh.cpp`
- Modify: `CMakeLists.txt` (`slab_mesh.cpp` → `bumpy_core`; `tests/cpp/slab_mesh_test.cpp` → `bumpy_tests`)
- Test: `tests/cpp/slab_mesh_test.cpp`

**Interfaces:**
- Consumes: `SceneQuad`, `QuadKind`, `opaque_bounds`, `kSlabDepth`.
- Produces (namespace `bumpy`):
  - shade constants `kShadeFront = 1.0f`, `kShadeTop = 1.18f`, `kShadeBottom = 0.62f`, `kShadeSide = 0.8f`
  - `struct QuadFace { std::array<float, 12> corners; std::array<float, 8> uv; float shade; }` — corners are 4×(x,y,z) in **GL space** (x right, y **up**, origin at board centre: `x_gl = x_px − 160`, `y_gl = 100 − y_px`), order TL,TR,BR,BL of the on-screen face; uv 4×(u,v) into the frame texture (v=0 at the top row).
  - `std::vector<QuadFace> quad_faces(const SceneQuad& quad, const MenuImage& frame)` — billboard → 1 face; slab → 5 faces (front + top/bottom/left/right with UVs pinned to the bbox edge pixel row/column — the "extruded edge pixels", shaded per face).
- Consumed by Task 12 (vertex buffer building).

- [ ] **Step 1: Write the failing test** — `tests/cpp/slab_mesh_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "resources/sprite_frame.h"
#include "video3d/scene3d.h"
#include "video3d/slab_mesh.h"

namespace {

bumpy::MenuImage solid_frame(int w, int h) {
    bumpy::MenuImage img;
    img.width = w;
    img.height = h;
    img.pixels.assign(static_cast<std::size_t>(w) * h, 3);  // fully opaque
    return img;
}

}  // namespace

TEST_CASE("a billboard is one front face covering the frame rect") {
    const auto img = solid_frame(16, 15);
    const bumpy::SceneQuad q{7, 80.0f, 100.0f, 16, 15, bumpy::QuadKind::billboard, 9.0f};
    const auto faces = bumpy::quad_faces(q, img);
    REQUIRE(faces.size() == 1);
    const auto& f = faces[0];
    REQUIRE(f.shade == bumpy::kShadeFront);
    // TL in GL space: x_gl = 80-160 = -80, y_gl = 100-100 = 0, z = 9.
    REQUIRE(f.corners[0] == -80.0f);
    REQUIRE(f.corners[1] == 0.0f);
    REQUIRE(f.corners[2] == 9.0f);
    // BR: x_gl = (80+16)-160 = -64, y_gl = 100-(100+15) = -15.
    REQUIRE(f.corners[6] == -64.0f);
    REQUIRE(f.corners[7] == -15.0f);
    // Full-frame UVs.
    REQUIRE(f.uv[0] == 0.0f);
    REQUIRE(f.uv[1] == 0.0f);
    REQUIRE(f.uv[4] == 1.0f);
    REQUIRE(f.uv[5] == 1.0f);
}

TEST_CASE("a slab is five faces with pinned-edge UVs and per-face shading") {
    const auto img = solid_frame(40, 8);
    const bumpy::SceneQuad q{7, 0.0f, 24.0f, 40, 8, bumpy::QuadKind::slab,
                             bumpy::kSlabDepth};
    const auto faces = bumpy::quad_faces(q, img);
    REQUIRE(faces.size() == 5);

    const auto& front = faces[0];
    REQUIRE(front.shade == bumpy::kShadeFront);
    REQUIRE(front.corners[2] == bumpy::kSlabDepth);  // front plane

    const auto& top = faces[1];
    REQUIRE(top.shade == bumpy::kShadeTop);
    // Top face v is pinned to the top edge pixel centre: (0 + 0.5) / 8.
    REQUIRE(top.uv[1] == 0.5f / 8.0f);
    REQUIRE(top.uv[5] == 0.5f / 8.0f);
    // Top face spans z from the back (front - depth = 0) to the front.
    REQUIRE(top.corners[2] == 0.0f);
    REQUIRE(top.corners[10] == bumpy::kSlabDepth);

    REQUIRE(faces[2].shade == bumpy::kShadeBottom);
    REQUIRE(faces[3].shade == bumpy::kShadeSide);
    REQUIRE(faces[4].shade == bumpy::kShadeSide);
}
```

- [ ] **Step 2: Register in CMake; verify compile failure.**

- [ ] **Step 3: Implement.** `src/video3d/slab_mesh.h`:

```cpp
#pragma once

#include "video/menu_renderer.h"  // MenuImage
#include "video3d/scene3d.h"

#include <array>
#include <vector>

namespace bumpy {

// Per-face constant shading of an extruded slab (cheap directional light).
inline constexpr float kShadeFront = 1.0f;
inline constexpr float kShadeTop = 1.18f;
inline constexpr float kShadeBottom = 0.62f;
inline constexpr float kShadeSide = 0.8f;

// One textured face in GL space (x right, y up, board centre at the origin);
// corners are TL,TR,BR,BL of the face as seen on screen, 4 x (x,y,z); uv 4 x (u,v).
struct QuadFace {
    std::array<float, 12> corners{};
    std::array<float, 8> uv{};
    float shade{kShadeFront};
};

// Billboard -> 1 front face over the whole frame rect. Slab -> 5 faces: the front
// face textured with the opaque bbox, and top/bottom/left/right faces whose UVs
// are pinned to the bbox's edge pixel row/column -- the sprite's own edge pixels
// stretched across the extrusion depth (the spec's only allowed new geometry).
[[nodiscard]] std::vector<QuadFace> quad_faces(const SceneQuad& quad, const MenuImage& frame);

}  // namespace bumpy
```

`src/video3d/slab_mesh.cpp`:

```cpp
#include "video3d/slab_mesh.h"

namespace bumpy {

namespace {

// Board-pixel -> GL-space conversion (y flips, origin moves to the board centre).
float gx(float x) { return x - 160.0f; }
float gy(float y) { return 100.0f - y; }

QuadFace face(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2,
              float z2, float x3, float y3, float z3, float u0, float v0, float u1, float v1,
              float u2, float v2, float u3, float v3, float shade) {
    QuadFace f;
    f.corners = {x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3};
    f.uv = {u0, v0, u1, v1, u2, v2, u3, v3};
    f.shade = shade;
    return f;
}

}  // namespace

std::vector<QuadFace> quad_faces(const SceneQuad& quad, const MenuImage& frame) {
    std::vector<QuadFace> faces;
    const float fw = static_cast<float>(frame.width);
    const float fh = static_cast<float>(frame.height);

    if (quad.kind == QuadKind::billboard) {
        const float l = gx(quad.x);
        const float r = gx(quad.x + static_cast<float>(quad.w));
        const float t = gy(quad.y);
        const float b = gy(quad.y + static_cast<float>(quad.h));
        faces.push_back(face(l, t, quad.z, r, t, quad.z, r, b, quad.z, l, b, quad.z,
                             0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, kShadeFront));
        return faces;
    }

    // Slab: extrude the opaque bbox from (front - kSlabDepth) to the front plane.
    const OpaqueBounds bb = opaque_bounds(frame);
    const float x0 = quad.x + static_cast<float>(bb.x);
    const float y0 = quad.y + static_cast<float>(bb.y);
    const float x1 = x0 + static_cast<float>(bb.w);
    const float y1 = y0 + static_cast<float>(bb.h);
    const float zf = quad.z;
    const float zb = quad.z - kSlabDepth;

    const float u0 = static_cast<float>(bb.x) / fw;
    const float v0 = static_cast<float>(bb.y) / fh;
    const float u1 = static_cast<float>(bb.x + bb.w) / fw;
    const float v1 = static_cast<float>(bb.y + bb.h) / fh;
    // Edge pixel centres, for the "stretched edge pixels" side faces.
    const float v_top = (static_cast<float>(bb.y) + 0.5f) / fh;
    const float v_bot = (static_cast<float>(bb.y + bb.h) - 0.5f) / fh;
    const float u_left = (static_cast<float>(bb.x) + 0.5f) / fw;
    const float u_right = (static_cast<float>(bb.x + bb.w) - 0.5f) / fw;

    const float l = gx(x0);
    const float r = gx(x1);
    const float t = gy(y0);
    const float b = gy(y1);

    // Front.
    faces.push_back(face(l, t, zf, r, t, zf, r, b, zf, l, b, zf,
                         u0, v0, u1, v0, u1, v1, u0, v1, kShadeFront));
    // Top: back edge (z=zb) to front edge (z=zf), v pinned to the top pixel row.
    faces.push_back(face(l, t, zb, r, t, zb, r, t, zf, l, t, zf,
                         u0, v_top, u1, v_top, u1, v_top, u0, v_top, kShadeTop));
    // Bottom.
    faces.push_back(face(l, b, zf, r, b, zf, r, b, zb, l, b, zb,
                         u0, v_bot, u1, v_bot, u1, v_bot, u0, v_bot, kShadeBottom));
    // Left: u pinned to the left pixel column.
    faces.push_back(face(l, t, zb, l, t, zf, l, b, zf, l, b, zb,
                         u_left, v0, u_left, v0, u_left, v1, u_left, v1, kShadeSide));
    // Right.
    faces.push_back(face(r, t, zf, r, t, zb, r, b, zb, r, b, zf,
                         u_right, v0, u_right, v0, u_right, v1, u_right, v1, kShadeSide));
    return faces;
}

}  // namespace bumpy
```

- [ ] **Step 4: Build + tests PASS.**

- [ ] **Step 5: Commit.** `git commit -m "feat(video3d): slab/billboard face geometry with pinned-edge UVs"`

---

### Task 12: SceneRenderer (GL), shader files, `--render-3d` dump

**Files:**
- Create: `shaders3d/scene.vert`, `shaders3d/wall.frag`, `shaders3d/sprite.frag`, `src/platform_gl3/scene_renderer.h`, `src/platform_gl3/scene_renderer.cpp`
- Modify: `CMakeLists.txt` (`scene_renderer.cpp` → `bumpy_platform_sdl3`; POST_BUILD copy of `shaders3d/`), `src/app/main.cpp` (`--render-3d` tool)

**Interfaces:**
- Consumes: `Gl33`, `gl_util`, `Mat4` helpers, `Scene3d`, `SpriteCache`, `SceneQuad`, `quad_faces`, `Viewport`, scene constants.
- Produces (namespace `bumpy`):
  - `struct SceneCamera { float x{}, y{}; }` — eased parallax offset in board pixels
  - `class SceneRenderer { SceneRenderer(const Gl33& gl, const std::filesystem::path& shader_dir); void set_scene(const Scene3d& scene, SpriteCache& sprites); void render(std::span<const SceneQuad> quads, float light_x, float light_y, const SceneCamera& cam, const Viewport& vp); bool reload_shaders(); }` — ctor throws on missing/uncompilable shaders; `render` assumes the caller bound the destination framebuffer and will swap/read afterwards.
  - CLI: `bumpy_port --render-3d <level> <MONDE.VEC> <board> <out.bmp>`
  - Shader dir resolution helper in main/sdl_app: exe-dir `shaders3d/`, else `<asset_root>/shaders3d`.

- [ ] **Step 1: Write the shader files.** `shaders3d/scene.vert` (shared by all scene programs):

```glsl
#version 330 core
layout(location = 0) in vec3 a_pos;    // GL space: x right, y up, board centre origin
layout(location = 1) in vec2 a_uv;
layout(location = 2) in float a_shade;
uniform mat4 u_mvp;
out vec2 v_uv;
out float v_shade;
out vec3 v_world;
void main() {
    v_uv = a_uv;
    v_shade = a_shade;
    v_world = a_pos;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
```

`shaders3d/wall.frag` (mural: baked blur in the texture; live spotlight + vignette here):

```glsl
#version 330 core
in vec2 v_uv;
in float v_shade;
in vec3 v_world;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec2 u_light;        // ball position, GL space
uniform float u_ambient;     // base wall brightness
uniform float u_spot;        // spotlight strength added near the ball
uniform float u_spot_radius; // px
uniform vec2 u_vp_offset;    // letterbox viewport origin in window px
uniform vec2 u_vp_size;
void main() {
    vec4 c = texture(u_tex, v_uv);
    float d = distance(v_world.xy, u_light);
    float light = u_ambient + u_spot * exp(-(d * d) / (u_spot_radius * u_spot_radius));
    vec2 ndc = ((gl_FragCoord.xy - u_vp_offset) / u_vp_size) * 2.0 - 1.0;
    float vig = 1.0 - 0.35 * dot(ndc, ndc);
    o_color = vec4(c.rgb * light * vig * v_shade, 1.0);
}
```

`shaders3d/sprite.frag` (crisp art: NEAREST texture, alpha discard, face shade, gentle vignette):

```glsl
#version 330 core
in vec2 v_uv;
in float v_shade;
in vec3 v_world;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec2 u_vp_offset;
uniform vec2 u_vp_size;
void main() {
    vec4 c = texture(u_tex, v_uv);
    if (c.a < 0.5) {
        discard;
    }
    vec2 ndc = ((gl_FragCoord.xy - u_vp_offset) / u_vp_size) * 2.0 - 1.0;
    float vig = 1.0 - 0.25 * dot(ndc, ndc);
    o_color = vec4(c.rgb * v_shade * vig, 1.0);
}
```

- [ ] **Step 2: Write `src/platform_gl3/scene_renderer.h`:**

```cpp
#pragma once

#include "platform_gl3/gl33.h"
#include "video/viewport.h"
#include "video3d/scene3d.h"

#include <filesystem>
#include <span>
#include <unordered_map>
#include <vector>

namespace bumpy {

struct SceneCamera {
    float x{};
    float y{};  // eased parallax offset, board pixels (y down)
};

// Draws the diorama: blurred mural wall, then slabs/billboards with a depth
// buffer. Owns the scene's GL resources. Shaders load from shader_dir
// (scene.vert, wall.frag, sprite.frag); any failure throws std::runtime_error
// (the caller logs and drops back to the flat path -- 3D never kills the game).
class SceneRenderer {
public:
    SceneRenderer(const Gl33& gl, std::filesystem::path shader_dir);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // Per-board: rebuild the wall texture and drop sprite textures (board palette).
    // `sprites` must outlive the renderer's use of it (frames feed lazy textures).
    void set_scene(const Scene3d& scene, SpriteCache& sprites);

    // Per-frame. Caller binds the target framebuffer (backbuffer or FBO) first and
    // swaps/reads after. light = ball position in board pixels.
    void render(std::span<const SceneQuad> quads, float light_x, float light_y,
                const SceneCamera& cam, const Viewport& vp);

    // Recompile from shader_dir; on failure keeps the old programs, returns false.
    bool reload_shaders();

private:
    GLuint sprite_texture(int frame_index);
    void destroy_scene_textures();

    const Gl33& gl_;
    std::filesystem::path shader_dir_;
    SpriteCache* sprites_{};
    std::array<Rgba, 256> palette_{};
    GLuint wall_program_{};
    GLuint sprite_program_{};
    GLuint vao_{};
    GLuint vbo_{};
    GLuint wall_tex_{};
    std::unordered_map<int, GLuint> sprite_textures_;
};

}  // namespace bumpy
```

- [ ] **Step 3: Write `src/platform_gl3/scene_renderer.cpp`:**

```cpp
#include "platform_gl3/scene_renderer.h"

#include "platform_gl3/gl_util.h"
#include "video3d/mat4.h"
#include "video3d/slab_mesh.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace bumpy {

namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read shader: " + path.string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// 6 floats per vertex: pos(3) uv(2) shade(1); 6 vertices (2 triangles) per face.
void append_face(std::vector<float>& verts, const QuadFace& f) {
    const int tri[6] = {0, 1, 2, 0, 2, 3};
    for (const int i : tri) {
        verts.push_back(f.corners[static_cast<std::size_t>(i) * 3]);
        verts.push_back(f.corners[static_cast<std::size_t>(i) * 3 + 1]);
        verts.push_back(f.corners[static_cast<std::size_t>(i) * 3 + 2]);
        verts.push_back(f.uv[static_cast<std::size_t>(i) * 2]);
        verts.push_back(f.uv[static_cast<std::size_t>(i) * 2 + 1]);
        verts.push_back(f.shade);
    }
}

}  // namespace

SceneRenderer::SceneRenderer(const Gl33& gl, std::filesystem::path shader_dir)
    : gl_(gl), shader_dir_(std::move(shader_dir)) {
    const auto vert = read_text_file(shader_dir_ / "scene.vert");
    wall_program_ = link_program(gl_, vert, read_text_file(shader_dir_ / "wall.frag"));
    sprite_program_ = link_program(gl_, vert, read_text_file(shader_dir_ / "sprite.frag"));
    gl_.GenVertexArrays(1, &vao_);
    gl_.BindVertexArray(vao_);
    gl_.GenBuffers(1, &vbo_);
    gl_.BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl_.EnableVertexAttribArray(0);
    gl_.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl_.EnableVertexAttribArray(1);
    gl_.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                            reinterpret_cast<const void*>(3 * sizeof(float)));
    gl_.EnableVertexAttribArray(2);
    gl_.VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                            reinterpret_cast<const void*>(5 * sizeof(float)));
}

SceneRenderer::~SceneRenderer() {
    destroy_scene_textures();
    if (vbo_ != 0) {
        gl_.DeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0) {
        gl_.DeleteVertexArrays(1, &vao_);
    }
    if (wall_program_ != 0) {
        gl_.DeleteProgram(wall_program_);
    }
    if (sprite_program_ != 0) {
        gl_.DeleteProgram(sprite_program_);
    }
}

void SceneRenderer::destroy_scene_textures() {
    if (wall_tex_ != 0) {
        glDeleteTextures(1, &wall_tex_);
        wall_tex_ = 0;
    }
    for (auto& [frame, tex] : sprite_textures_) {
        glDeleteTextures(1, &tex);
    }
    sprite_textures_.clear();
}

void SceneRenderer::set_scene(const Scene3d& scene, SpriteCache& sprites) {
    destroy_scene_textures();
    sprites_ = &sprites;
    palette_ = scene.palette;
    // LINEAR: the wall is pre-blurred; linear sampling keeps the parallax smooth.
    wall_tex_ = make_rgba_texture(scene.wall_rgba, 320, 200, /*linear_filter=*/true);
}

GLuint SceneRenderer::sprite_texture(int frame_index) {
    if (const auto it = sprite_textures_.find(frame_index); it != sprite_textures_.end()) {
        return it->second;
    }
    const MenuImage* img = sprites_ ? sprites_->frame(frame_index) : nullptr;
    if (!img) {
        return 0;
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(img->width) * img->height * 4);
    for (std::size_t i = 0; i < img->pixels.size(); ++i) {
        const std::uint8_t idx = img->pixels[i];
        if (idx == sprite_transparent_index) {
            continue;  // transparent: rgba already zeroed
        }
        const Rgba c = palette_[idx];
        rgba[i * 4] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
        rgba[i * 4 + 3] = 0xff;
    }
    const GLuint tex = make_rgba_texture(rgba, img->width, img->height);
    sprite_textures_.emplace(frame_index, tex);
    return tex;
}

void SceneRenderer::render(std::span<const SceneQuad> quads, float light_x, float light_y,
                           const SceneCamera& cam, const Viewport& vp) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(vp.x, vp.y, vp.w, vp.h);

    const float dist = scene_camera_distance();
    // vp always comes from compute_letterbox_viewport at 320:logical_h, so aspect is
    // 1.6 (square pixels) or ~1.333 (Alt+A CRT); with the vertical frustum fixed at
    // 200 world px the 4:3 case presents the same stage stretched taller -- exactly
    // the flat path's Alt+A behavior.
    const float aspect = vp.h > 0 ? static_cast<float>(vp.w) / vp.h : 1.6f;
    const Mat4 proj =
        mat4_perspective(kCameraFovYDeg * 3.14159265f / 180.0f, aspect, 1.0f, 2000.0f);
    // Camera: parallax offset in board px (y down) -> GL (y up), backed off by dist.
    const Mat4 view = mat4_translate(-cam.x, cam.y, -dist);
    const Mat4 mvp = mat4_multiply(proj, view);
    const float light_gx = light_x - 160.0f;
    const float light_gy = 100.0f - light_y;

    gl_.BindVertexArray(vao_);
    gl_.BindBuffer(GL_ARRAY_BUFFER, vbo_);

    // --- Wall: scaled so the frustum (plus the parallax travel) never sees past
    // its edge; drawn without depth so everything else stacks in front.
    {
        const float cover = (dist - kWallZ) / dist;
        const float margin = (kParallaxGain * 160.0f + 4.0f) / 160.0f;
        const float sx = 160.0f * (cover + margin);
        const float sy = 100.0f * (cover + margin);
        QuadFace wall;
        wall.corners = {-sx, sy, kWallZ, sx, sy, kWallZ, sx, -sy, kWallZ, -sx, -sy, kWallZ};
        wall.uv = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        wall.shade = 1.0f;
        std::vector<float> verts;
        append_face(verts, wall);
        gl_.BufferData(GL_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(),
                       GL_STREAM_DRAW);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        gl_.UseProgram(wall_program_);
        gl_.Uniform1i(gl_.GetUniformLocation(wall_program_, "u_tex"), 0);
        gl_.UniformMatrix4fv(gl_.GetUniformLocation(wall_program_, "u_mvp"), 1, GL_FALSE,
                             mvp.m.data());
        gl_.Uniform2f(gl_.GetUniformLocation(wall_program_, "u_light"), light_gx, light_gy);
        gl_.Uniform1f(gl_.GetUniformLocation(wall_program_, "u_ambient"), 0.55f);
        gl_.Uniform1f(gl_.GetUniformLocation(wall_program_, "u_spot"), 0.55f);
        gl_.Uniform1f(gl_.GetUniformLocation(wall_program_, "u_spot_radius"), 90.0f);
        gl_.Uniform2f(gl_.GetUniformLocation(wall_program_, "u_vp_offset"),
                      static_cast<float>(vp.x), static_cast<float>(vp.y));
        gl_.Uniform2f(gl_.GetUniformLocation(wall_program_, "u_vp_size"),
                      static_cast<float>(vp.w), static_cast<float>(vp.h));
        gl_.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, wall_tex_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // --- Slabs and billboards: depth-tested, alpha handled by discard, so draw
    // order does not matter. One draw per quad (a few dozen -- fine at this scale).
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    gl_.UseProgram(sprite_program_);
    gl_.Uniform1i(gl_.GetUniformLocation(sprite_program_, "u_tex"), 0);
    gl_.UniformMatrix4fv(gl_.GetUniformLocation(sprite_program_, "u_mvp"), 1, GL_FALSE,
                         mvp.m.data());
    gl_.Uniform2f(gl_.GetUniformLocation(sprite_program_, "u_vp_offset"),
                  static_cast<float>(vp.x), static_cast<float>(vp.y));
    gl_.Uniform2f(gl_.GetUniformLocation(sprite_program_, "u_vp_size"),
                  static_cast<float>(vp.w), static_cast<float>(vp.h));
    for (const auto& quad : quads) {
        const GLuint tex = sprite_texture(quad.frame_index);
        const MenuImage* img = sprites_ ? sprites_->frame(quad.frame_index) : nullptr;
        if (tex == 0 || !img) {
            continue;
        }
        std::vector<float> verts;
        for (const auto& f : quad_faces(quad, *img)) {
            append_face(verts, f);
        }
        gl_.BufferData(GL_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(),
                       GL_STREAM_DRAW);
        gl_.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 6));
    }
}

bool SceneRenderer::reload_shaders() {
    try {
        const auto vert = read_text_file(shader_dir_ / "scene.vert");
        const GLuint wall = link_program(gl_, vert, read_text_file(shader_dir_ / "wall.frag"));
        const GLuint sprite =
            link_program(gl_, vert, read_text_file(shader_dir_ / "sprite.frag"));
        gl_.DeleteProgram(wall_program_);
        gl_.DeleteProgram(sprite_program_);
        wall_program_ = wall;
        sprite_program_ = sprite;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace bumpy
```

- [ ] **Step 4: CMake.** Add `scene_renderer.cpp` to `bumpy_platform_sdl3`; add the shader copy:

```cmake
add_custom_command(TARGET bumpy_port POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders3d
    $<TARGET_FILE_DIR:bumpy_port>/shaders3d)
```

- [ ] **Step 5: `--render-3d` in main.cpp.** Add includes `"platform_gl3/scene_renderer.h"`, `"video3d/scene3d.h"` and:

First, extract the BMP header block that `write_24bit_bmp` already emits into a shared helper (replace the header-writing lines inside `write_24bit_bmp` with a call to it — byte-identical output):

```cpp
// The 54-byte BMP file+info header for a 24-bit image (shared by the indexed and
// RGBA dump writers so the two never drift).
void write_bmp_header(std::ostream& output, std::uint32_t width, std::uint32_t height,
                      std::uint32_t pixel_bytes) {
    output.write("BM", 2);
    write_u32(output, 14U + 40U + pixel_bytes);
    write_u16(output, 0);
    write_u16(output, 0);
    write_u32(output, 54);
    write_u32(output, 40);
    write_u32(output, width);
    write_u32(output, height);
    write_u16(output, 1);
    write_u16(output, 24);
    write_u32(output, 0);
    write_u32(output, pixel_bytes);
    write_u32(output, 0);
    write_u32(output, 0);
    write_u32(output, 0);
    write_u32(output, 0);
}

// RGBA (rows top-to-bottom) -> 24-bit BMP, for GL readback dumps.
void write_24bit_bmp_rgba(const std::filesystem::path& path,
                          const std::vector<std::uint8_t>& rgba, int w, int h) {
    const auto row_stride = ((static_cast<std::uint32_t>(w) * 3U) + 3U) & ~3U;
    const auto pixel_bytes = row_stride * static_cast<std::uint32_t>(h);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create BMP: " + path.string());
    }
    write_bmp_header(output, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                     pixel_bytes);
    std::vector<std::uint8_t> row(row_stride);
    for (int y = h - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < w; ++x) {
            const std::size_t src = (static_cast<std::size_t>(y) * w + x) * 4;
            row[static_cast<std::size_t>(x) * 3] = rgba[src + 2];
            row[static_cast<std::size_t>(x) * 3 + 1] = rgba[src + 1];
            row[static_cast<std::size_t>(x) * 3 + 2] = rgba[src];
        }
        output.write(reinterpret_cast<const char*>(row.data()),
                     static_cast<std::streamsize>(row.size()));
    }
}

// Shader dir: next to the exe (installed layout) or the repo checkout.
std::filesystem::path resolve_shader_dir(const std::filesystem::path& asset_root,
                                         std::string_view executable_path) {
    std::error_code error;
    const auto exe =
        std::filesystem::weakly_canonical(std::filesystem::path(executable_path), error);
    if (!error && exe.has_parent_path() &&
        std::filesystem::is_directory(exe.parent_path() / "shaders3d", error)) {
        return exe.parent_path() / "shaders3d";
    }
    return asset_root / "shaders3d";
}

// Offline 3D-diorama frame: board 0-input start state (ball hanging at its entry
// cell), rendered headless at 1280x800 -- the by-eye gate for the diorama look.
int render_3d_to_bmp(const std::filesystem::path& asset_root, std::string_view exe_path,
                     int level_number, const std::filesystem::path& monde_path,
                     std::size_t board_index, const std::filesystem::path& out_path) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window =
        SDL_CreateWindow("render3d", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    int rc = 1;
    if (window) {
        bumpy::GlPresenter presenter(window);
        const auto level = bumpy::LevelResources::load(asset_root, level_number);
        const auto backdrop = bumpy::decode_vec_resource(monde_path);
        const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
        bumpy::SpriteCache sprites(bank.bytes());
        const auto scene = bumpy::build_scene3d(level, board_index, backdrop.decoded_bytes());
        bumpy::SceneRenderer renderer(presenter.gl(),
                                      resolve_shader_dir(asset_root, exe_path));
        renderer.set_scene(scene, sprites);

        bumpy::LevelGame game(level.bum_entities(board_index));
        bumpy::BumEntities live{};
        std::copy(game.grid().begin(), game.grid().begin() + bumpy::BumEntities::record_size,
                  live.bytes.begin());
        std::optional<bumpy::MonsterPose> monster;
        if (game.monster_present()) {
            monster = bumpy::MonsterPose{game.monster_frame(), game.monster_x(),
                                         game.monster_y()};
        }
        const auto quads = bumpy::build_live_quads(
            live, {}, monster, bumpy::BallPose{game.ball_frame(), game.ball_x(), game.ball_y()},
            sprites);

        const int w = 1280;
        const int h = 800;
        auto target = bumpy::make_offscreen_target(presenter.gl(), w, h);
        presenter.gl().BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        renderer.render(quads, static_cast<float>(game.ball_x()),
                        static_cast<float>(game.ball_y()), {}, bumpy::Viewport{0, 0, w, h});
        const auto rgba = bumpy::read_target_rgba(presenter.gl(), target);
        presenter.gl().BindFramebuffer(GL_FRAMEBUFFER, 0);
        bumpy::destroy_offscreen_target(presenter.gl(), target);
        write_24bit_bmp_rgba(out_path, rgba, w, h);
        std::cout << "wrote " << out_path.string() << " (" << quads.size() << " quads)\n";
        rc = 0;
        SDL_DestroyWindow(window);
    } else {
        std::cerr << SDL_GetError() << '\n';
    }
    SDL_Quit();
    return rc;
}
```

CLI branch (near `--render-board`):

```cpp
        if (argc == 6 && std::string_view(argv[1]) == "--render-3d") {
            // --render-3d <level> <MONDE.VEC> <board> <out.bmp>: headless diorama dump.
            return render_3d_to_bmp(asset_root, argv[0], std::stoi(argv[2]), argv[3],
                                    static_cast<std::size_t>(std::stoi(argv[4])), argv[5]);
        }
```

- [ ] **Step 6: Build; run** `bumpy_port.exe --render-3d 1 MONDE1.VEC 0 analysis/generated/render3d_b0.bmp` (adjust MONDE path to where the asset lives at the repo root). Open the BMP: expect the blurred mural behind, crisp platform bars with visible extruded tops, collectibles/ball floating in front, spotlight near the ball, vignette at the edges. Iterate constants (`kWallZ`, shades, ambient/spot) by eye if something is off — they are the tuning knobs, not fixed.

- [ ] **Step 7: Commit.** `git commit -m "feat(video3d): SceneRenderer + diorama shaders + --render-3d headless dump"`

---

### Task 13: Wire the live 3D path into SdlApp (Alt+3 becomes real)

**Files:**
- Modify: `src/platform_sdl3/sdl_app.h`, `src/platform_sdl3/sdl_app.cpp`

**Interfaces:**
- Consumes: `SceneRenderer`, `Scene3d`/`build_scene3d`/`build_live_quads`/`SpriteCache`, `resolve_shader_dir` logic (replicate: `SDL_GetBasePath()` dir, else `asset_root / "shaders3d"`), scene constants, `compute_letterbox_viewport`.
- Produces: in-game Alt+3 toggling between flat and diorama on the level screen. Flat `frame` composition still runs every frame in 3D mode (screen-change darken snapshots stay correct); only presentation differs.

- [ ] **Step 1: Scene state + lazy renderer in `run()`** (after the `render_level` lambda):

```cpp
    // --- 3D diorama state (Alt+3). The flat 320x200 composition in `frame` still
    // runs every frame even in 3D mode: the screen-change darken snapshots it, and
    // it keeps the two paths trivially in sync. 3D only swaps the PRESENTATION.
    std::unique_ptr<SceneRenderer> scene_renderer;
    bool scene_renderer_failed = false;  // shader/setup failure: 3D disabled for the run
    SpriteCache sprite_cache(sprite_bank);
    int scene_world = -1;
    std::size_t scene_board = static_cast<std::size_t>(-1);
    SceneCamera cam{};
    auto shader_dir = [&]() -> std::filesystem::path {
        if (const char* base = SDL_GetBasePath()) {
            const std::filesystem::path candidate = std::filesystem::path(base) / "shaders3d";
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error)) {
                return candidate;
            }
        }
        return asset_root / "shaders3d";
    };

    // Present the level through the diorama; false = caller presents flat instead
    // (no GL, renderer failed, mode off, or no live board this frame).
    auto present_3d_level = [&]() -> bool {
        if (!render3d || !gl_ || scene_renderer_failed || !game) {
            return false;
        }
        if (!scene_renderer) {
            try {
                scene_renderer = std::make_unique<SceneRenderer>(gl_->gl(), shader_dir());
            } catch (const std::exception& error) {
                std::cerr << "warning: 3D mode disabled: " << error.what() << '\n';
                scene_renderer_failed = true;
                return false;
            }
        }
        if (scene_world != world.world() || scene_board != app.board_index()) {
            const Scene3d scene =
                build_scene3d(world.level(), app.board_index(), world.backdrop());
            scene_renderer->set_scene(scene, sprite_cache);
            scene_world = world.world();
            scene_board = app.board_index();
        }
        // Live quads: the exact same inputs the flat render_level composes.
        BumEntities live = live_entities();
        std::array<ObjectAnimSprite, 7> anims{};
        const std::size_t anim_count = game->object_anims(anims);
        for (std::size_t k = 0; k < anim_count; ++k) {
            const std::size_t off = anims[k].layer_b ? BumEntities::layer_b_offset
                                                     : BumEntities::layer_a_offset;
            live.bytes[anims[k].cell + off] = 0;
        }
        std::optional<MonsterPose> monster;
        if (game->monster_present()) {
            monster = MonsterPose{game->monster_frame(), game->monster_x(), game->monster_y()};
        }
        const auto quads = build_live_quads(
            live, {anims.data(), anim_count}, monster,
            BallPose{game->ball_frame(), game->ball_x(), game->ball_y()}, sprite_cache);

        // Eased parallax toward the ball's offset from the board centre.
        const float tx = kParallaxGain * (static_cast<float>(game->ball_x()) - 160.0f);
        const float ty = kParallaxGain * (static_cast<float>(game->ball_y()) - 100.0f);
        cam.x += kParallaxEase * (tx - cam.x);
        cam.y += kParallaxEase * (ty - cam.y);

        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSizeInPixels(window_, &win_w, &win_h);
        const Viewport vp =
            compute_letterbox_viewport(win_w, win_h, 320, square_pixels ? 200 : 240);
        gl_->gl().BindFramebuffer(GL_FRAMEBUFFER, 0);
        scene_renderer->render(quads, static_cast<float>(game->ball_x()),
                               static_cast<float>(game->ball_y()), cam, vp);
        SDL_GL_SwapWindow(window_);
        return true;
    };
```

(Add includes to `sdl_app.cpp`: `"platform_gl3/scene_renderer.h"`, `"video3d/scene3d.h"`, `"video/viewport.h"`, `<memory>`.)

- [ ] **Step 2: Presentation switch.** The level branch of the per-screen render currently ends at `render_level();` followed by the shared `present_frame();`. Change the tail:

```cpp
        } else {
            render_level();
        }

        if (app.screen() != Screen::level || !present_3d_level()) {
            present_frame();
        }
```

(The terminal-frame `render_level()` + `finish_level` path is untouched — it feeds the darken snapshot, which plays flat by design.)

- [ ] **Step 3: Build + play.** Manual QA, comparing against `--render-3d` stills: start a board, Alt+3 in and out mid-play; check platforms/springs/collectibles/monster/ball all present and moving identically to flat; parallax follows the ball smoothly; spring bump animations show; win a board and check the darken wipe still plays; Alt+A and fullscreen in 3D mode; die/game-over path. Then delete a shader file and relaunch with `--render3d`: expect a stderr warning and flat gameplay (no crash), Alt+3 dead for the session.

- [ ] **Step 4: Commit.** `git commit -m "feat(video3d): live Alt+3 diorama path in the SDL shell with ball parallax"`

---

### Task 14: Soft shadows on the wall

**Files:**
- Create: `shaders3d/shadow.frag`
- Modify: `src/video3d/scene3d.h`, `src/video3d/scene3d.cpp` (shadow silhouette bake), `src/platform_gl3/scene_renderer.h`, `src/platform_gl3/scene_renderer.cpp` (shadow pass), `tests/cpp/scene3d_test.cpp` (silhouette test)

**Interfaces:**
- Produces:
  - constants in `scene3d.h`: `kShadowOffsetX = 5.0f`, `kShadowOffsetY = 7.0f`, `kShadowAlpha = 0.35f`, `kShadowBlurSigma = 2.0f`, `inline constexpr int kShadowPad = 6;`
  - `std::vector<std::uint8_t> shadow_silhouette(const MenuImage& image, int pad, float sigma)` — single-channel `(w+2*pad) x (h+2*pad)` blurred alpha of the opaque silhouette
  - `SceneRenderer` gains a shadow program (vert = `scene.vert`, frag = `shadow.frag`) and a `GL_R8`-style shadow texture cache (`GL_RED` internal format, single channel); shadows drawn between the wall and the sprites.

- [ ] **Step 1: Failing test** (append to `scene3d_test.cpp`):

```cpp
TEST_CASE("shadow_silhouette pads and blurs the opaque mask") {
    const auto img = make_frame(4, 4, 1, 1, 2, 2);
    const int pad = 3;
    const auto mask = bumpy::shadow_silhouette(img, pad, 1.0f);
    const int w = 4 + 2 * pad;
    REQUIRE(mask.size() == static_cast<std::size_t>(w) * (4 + 2 * pad));
    // Centre of the opaque rect stays strong; a pixel just outside the silhouette
    // picked up blurred energy; the far corner stays empty.
    REQUIRE(mask[(pad + 1) * w + (pad + 1)] > 128);
    REQUIRE(mask[(pad + 0) * w + (pad + 1)] > 0);
    REQUIRE(mask[0] == 0);
}
```

- [ ] **Step 2: Implement the bake** in `scene3d.cpp` (declaration + constants in the header as listed above):

```cpp
std::vector<std::uint8_t> shadow_silhouette(const MenuImage& image, int pad, float sigma) {
    const int w = image.width + 2 * pad;
    const int h = image.height + 2 * pad;
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (image.pixels[static_cast<std::size_t>(y) * image.width + x] !=
                sprite_transparent_index) {
                mask[static_cast<std::size_t>(y + pad) * w + (x + pad)] = 255;
            }
        }
    }
    gaussian_blur_alpha(mask, w, h, sigma);
    return mask;
}
```

- [ ] **Step 3: `shaders3d/shadow.frag`:**

```glsl
#version 330 core
in vec2 v_uv;
in float v_shade;   // carries the shadow strength (kShadowAlpha)
in vec3 v_world;
out vec4 o_color;
uniform sampler2D u_tex;  // single-channel blurred silhouette in .r
void main() {
    o_color = vec4(0.0, 0.0, 0.0, texture(u_tex, v_uv).r * v_shade);
}
```

- [ ] **Step 4: Shadow pass in `SceneRenderer`.** Add members `GLuint shadow_program_{}` (link in ctor and in `reload_shaders` with the shared vert), `std::unordered_map<int, GLuint> shadow_textures_` (cleared in `destroy_scene_textures`). Texture builder: `shadow_silhouette(*img, kShadowPad, kShadowBlurSigma)` uploaded with `glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, mask.data())`, LINEAR filter, CLAMP_TO_EDGE. In `render()`, insert between the wall and the sprite pass:

```cpp
    // --- Shadows: each quad's blurred silhouette, offset toward down-right, flat
    // on the wall plane. Blended, no depth -- painter order (wall already drawn).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    gl_.UseProgram(shadow_program_);
    gl_.Uniform1i(gl_.GetUniformLocation(shadow_program_, "u_tex"), 0);
    gl_.UniformMatrix4fv(gl_.GetUniformLocation(shadow_program_, "u_mvp"), 1, GL_FALSE,
                         mvp.m.data());
    for (const auto& quad : quads) {
        const GLuint tex = shadow_texture(quad.frame_index);
        if (tex == 0) {
            continue;
        }
        SceneQuad shadow = quad;
        shadow.kind = QuadKind::billboard;  // flat silhouette, never extruded
        shadow.x = quad.x - kShadowPad + kShadowOffsetX;
        shadow.y = quad.y - kShadowPad + kShadowOffsetY;
        shadow.w = quad.w + 2 * kShadowPad;
        shadow.h = quad.h + 2 * kShadowPad;
        shadow.z = kWallZ + 0.6f;
        const MenuImage* img = sprites_->frame(quad.frame_index);
        auto faces = quad_faces(shadow, *img);
        faces[0].uv = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};  // pad-inclusive
        faces[0].shade = kShadowAlpha;
        std::vector<float> verts;
        append_face(verts, faces[0]);
        gl_.BufferData(GL_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(),
                       GL_STREAM_DRAW);
        gl_.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glDisable(GL_BLEND);
```

`shadow_texture(int)` in full (member, next to `sprite_texture`; `shadow_textures_` is cleared in `destroy_scene_textures` alongside the sprite cache):

```cpp
GLuint SceneRenderer::shadow_texture(int frame_index) {
    if (const auto it = shadow_textures_.find(frame_index); it != shadow_textures_.end()) {
        return it->second;
    }
    const MenuImage* img = sprites_ ? sprites_->frame(frame_index) : nullptr;
    if (!img) {
        return 0;
    }
    const auto mask = shadow_silhouette(*img, kShadowPad, kShadowBlurSigma);
    const int w = img->width + 2 * kShadowPad;
    const int h = img->height + 2 * kShadowPad;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, mask.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    shadow_textures_.emplace(frame_index, tex);
    return tex;
}
```

- [ ] **Step 5: Build, test, look.** `ctest` green; re-run `--render-3d` — soft dark silhouettes behind bars/collectibles/ball on the wall; play with Alt+3 — the ball's shadow tracks it. Tune offsets/alpha by eye if needed.

- [ ] **Step 6: Commit.** `git commit -m "feat(video3d): baked soft shadows of scene quads on the mural wall"`

---

### Task 15: Shader hot-reload (Debug), docs, final QA

**Files:**
- Modify: `src/platform_sdl3/sdl_app.cpp` (Alt+R in Debug), `PROJECT_STATUS.md`, `README.md` (controls/config sections; if README has no controls table, add one)

- [ ] **Step 1: Hot reload.** In the key handler chain, Debug only:

```cpp
#ifndef NDEBUG
                } else if (event.key.key == SDLK_R && (event.key.mod & SDL_KMOD_ALT)) {
                    // Debug look-iteration: recompile the diorama shaders in place.
                    if (scene_renderer) {
                        std::cerr << (scene_renderer->reload_shaders()
                                          ? "shaders reloaded\n"
                                          : "shader reload failed; keeping previous\n");
                    }
#endif
```

(Note: `scene_renderer` lives inside `run()`; place this handler accordingly — the event loop is already inside `run()`.)

- [ ] **Step 2: Docs.** `PROJECT_STATUS.md`: add a "3D render mode" section — GL 3.3 presenter (flat parity gated by `--present-parity`), diorama scene (wall/slabs/billboards/parallax/spotlight/shadows), Alt+3 / `--render3d` / `bumpy_port.cfg`, fallback behavior, and the phase-2 note (non-level screens still flat). `README.md`: extend controls with `Alt+3 — 3D diorama mode (in-level)`, `Alt+A`, `Alt+Enter`, and document `bumpy_port.cfg` + `--render3d` + `--render-3d`/`--present-parity` tools.

- [ ] **Step 3: Final QA sweep.**
  - `ctest --preset windows-debug` — all green.
  - `bumpy_port --present-parity` — all PASS.
  - `bumpy_port --render-3d 1 MONDE1.VEC 0 out.bmp` — by-eye check.
  - Play worlds 1 and 2 boards in 3D: springs, monster, nests/dig, block riding, picture puzzle, exit portal all render; win + lose + game-over transitions; Alt+3 toggles cleanly both ways; config persists across a restart; Release build (`cmake --build --preset windows-release`) runs with shaders copied next to the exe.
- [ ] **Step 4: Commit.** `git commit -m "feat(video3d): shader hot-reload (debug), docs for 3D mode + config"`

---

## Self-Review Notes

- **Spec coverage:** scene decomposition (T8/T10/T11), camera+parallax (T12/T13), effects: blur (T9/T10), spotlight+vignette (T12), shadows (T14); GL presenter + parity (T3–T6); switching Alt+3/`--render3d`/config (T2/T7); failure fallback (T6 ctor, T13 `scene_renderer_failed`); `--render-3d` dump (T12); non-goals respected (flat composition untouched — T13 keeps `render_level()` running; no new scene elements — wall/slabs/billboards/effects only).
- **Deviation from spec (approved during planning):** the GL loader is ~60 lines over SDL's own GL headers instead of vendored glad — zero third-party code; the spec is amended alongside this plan. Dump output is `.bmp` (consistent with every existing `--render-*` tool), not `.png`.
- **Known tuning knobs, not defects:** `kWallZ`, shades, ambient/spot/vignette strengths, parallax gain/ease, shadow offsets — all constants in `scene3d.h`/`slab_mesh.h`, expected to be adjusted by eye at T12/T13/T14.

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

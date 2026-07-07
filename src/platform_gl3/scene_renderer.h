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

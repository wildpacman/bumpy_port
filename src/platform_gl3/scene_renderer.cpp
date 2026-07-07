#include "platform_gl3/scene_renderer.h"

#include "platform_gl3/gl_util.h"
#include "resources/sprite_frame.h"
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
    // GL_CULL_FACE must stay disabled: Task 11's review verified all face normals
    // point inward under the right-hand rule, so culling would hide them.
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

#include "platform_gl3/scene_renderer.h"

#include "platform_gl3/gl_util.h"
#include "resources/sprite_frame.h"
#include "video3d/mat4.h"
#include "video3d/slab_mesh.h"

#include <cmath>
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
    try {
        sprite_program_ = link_program(gl_, vert, read_text_file(shader_dir_ / "sprite.frag"));
    } catch (...) {
        gl_.DeleteProgram(wall_program_);
        wall_program_ = 0;
        throw;
    }
    try {
        shadow_program_ = link_program(gl_, vert, read_text_file(shader_dir_ / "shadow.frag"));
    } catch (...) {
        gl_.DeleteProgram(wall_program_);
        gl_.DeleteProgram(sprite_program_);
        wall_program_ = 0;
        sprite_program_ = 0;
        throw;
    }
    try {
        bloom_program_ = link_program(gl_, vert, read_text_file(shader_dir_ / "bloom.frag"));
    } catch (...) {
        gl_.DeleteProgram(wall_program_);
        gl_.DeleteProgram(sprite_program_);
        gl_.DeleteProgram(shadow_program_);
        wall_program_ = 0;
        sprite_program_ = 0;
        shadow_program_ = 0;
        throw;
    }
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
    if (shadow_program_ != 0) {
        gl_.DeleteProgram(shadow_program_);
    }
    if (bloom_program_ != 0) {
        gl_.DeleteProgram(bloom_program_);
    }
}

void SceneRenderer::destroy_scene_textures() {
    if (wall_tex_ != 0) {
        glDeleteTextures(1, &wall_tex_);
        wall_tex_ = 0;
    }
    if (bloom_tex_ != 0) {
        glDeleteTextures(1, &bloom_tex_);
        bloom_tex_ = 0;
    }
    for (auto& [frame, tex] : sprite_textures_) {
        glDeleteTextures(1, &tex);
    }
    sprite_textures_.clear();
    for (auto& [frame, tex] : shadow_textures_) {
        glDeleteTextures(1, &tex);
    }
    shadow_textures_.clear();
}

void SceneRenderer::set_scene(const Scene3d& scene, SpriteCache& sprites) {
    destroy_scene_textures();
    sprites_ = &sprites;
    palette_ = scene.palette;
    // LINEAR: the wall is pre-blurred; linear sampling keeps the mirrored extension soft.
    wall_tex_ = make_rgba_texture(scene.wall_rgba, 320, 200, /*linear_filter=*/true);
    // Widescreen reveals wall past the 320x200 mural; continue it as a mirrored
    // copy (pre-blurred, so the seam is soft).
    glBindTexture(GL_TEXTURE_2D, wall_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    // Bloom: the wall's baked bright-pass, added over the wall. Same size/wrap so
    // it lines up with the mural and its mirrored widescreen extension.
    bloom_tex_ = make_rgba_texture(scene.bloom_rgba, 320, 200, /*linear_filter=*/true);
    glBindTexture(GL_TEXTURE_2D, bloom_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
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

void SceneRenderer::render(std::span<const SceneQuad> quads, float light_x, float light_y,
                           const Viewport& vp) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(vp.x, vp.y, vp.w, vp.h);

    const float dist = scene_camera_distance();
    // 4:3-corrected stage in any window: world px present kCrtPixelAspect taller
    // than wide (the flat path's Alt+A 4:3 look). The camera never moves; the fov
    // only grows when the window is narrower than 4:3, so the field is always
    // whole and centred and wider windows just reveal more wall at the sides.
    const SceneFrustum fr = scene_frustum(vp.w, vp.h);
    const float fovy = 2.0f * std::atan(fr.half_height / dist);
    const Mat4 proj = mat4_perspective(fovy, fr.aspect, 1.0f, 2000.0f);
    const Mat4 view = mat4_translate(0.0f, 0.0f, -dist);
    const Mat4 mvp = mat4_multiply(proj, view);
    const float light_gx = light_x - 160.0f;
    const float light_gy = 100.0f - light_y;

    gl_.BindVertexArray(vao_);
    gl_.BindBuffer(GL_ARRAY_BUFFER, vbo_);

    // --- Wall: sized to the frustum at wall depth; UVs keep the mural's 320x200
    // texels exactly behind the field (1 texel <-> 1 stage px on screen) and the
    // GL_MIRRORED_REPEAT wrap continues it past the edges. Drawn without depth so
    // everything else stacks in front.
    {
        const float cover = (dist - kWallZ) / dist;
        const float sx = fr.half_height * fr.aspect * cover + 2.0f;
        const float sy = fr.half_height * cover + 2.0f;
        const float u0 = 0.5f - sx / (320.0f * cover);
        const float u1 = 0.5f + sx / (320.0f * cover);
        const float v0 = 0.5f - sy / (200.0f * cover);
        const float v1 = 0.5f + sy / (200.0f * cover);
        QuadFace wall;
        wall.corners = {-sx, sy, kWallZ, sx, sy, kWallZ, sx, -sy, kWallZ, -sx, -sy, kWallZ};
        wall.uv = {u0, v0, u1, v0, u1, v1, u0, v1};
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

        // --- Wall bloom: the same quad, additive, using the baked bright-pass
        // (mural's own bright pixels spread wide). Depth stays off; the VBO still
        // holds the wall verts, so this reuses them exactly.
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        gl_.UseProgram(bloom_program_);
        gl_.Uniform1i(gl_.GetUniformLocation(bloom_program_, "u_tex"), 0);
        gl_.UniformMatrix4fv(gl_.GetUniformLocation(bloom_program_, "u_mvp"), 1, GL_FALSE,
                             mvp.m.data());
        gl_.Uniform2f(gl_.GetUniformLocation(bloom_program_, "u_vp_offset"),
                      static_cast<float>(vp.x), static_cast<float>(vp.y));
        gl_.Uniform2f(gl_.GetUniformLocation(bloom_program_, "u_vp_size"),
                      static_cast<float>(vp.w), static_cast<float>(vp.h));
        glBindTexture(GL_TEXTURE_2D, bloom_tex_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
    }

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
        // Overhead light: throw fans from the light's vertical axis by the
        // object centre's horizontal distance from it (centre -> straight down).
        const float shadow_center_x = quad.x + static_cast<float>(quad.w) * 0.5f;
        const float shadow_off_x =
            kShadowOffsetX + kShadowFanX * (shadow_center_x - kShadowLightX);
        shadow.x = quad.x - kShadowPad + shadow_off_x;
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
        GLuint sprite = 0;
        try {
            sprite = link_program(gl_, vert, read_text_file(shader_dir_ / "sprite.frag"));
        } catch (...) {
            gl_.DeleteProgram(wall);
            throw;
        }
        GLuint shadow = 0;
        try {
            shadow = link_program(gl_, vert, read_text_file(shader_dir_ / "shadow.frag"));
        } catch (...) {
            gl_.DeleteProgram(wall);
            gl_.DeleteProgram(sprite);
            throw;
        }
        GLuint bloom = 0;
        try {
            bloom = link_program(gl_, vert, read_text_file(shader_dir_ / "bloom.frag"));
        } catch (...) {
            gl_.DeleteProgram(wall);
            gl_.DeleteProgram(sprite);
            gl_.DeleteProgram(shadow);
            throw;
        }
        gl_.DeleteProgram(wall_program_);
        gl_.DeleteProgram(sprite_program_);
        gl_.DeleteProgram(shadow_program_);
        gl_.DeleteProgram(bloom_program_);
        wall_program_ = wall;
        sprite_program_ = sprite;
        shadow_program_ = shadow;
        bloom_program_ = bloom;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace bumpy

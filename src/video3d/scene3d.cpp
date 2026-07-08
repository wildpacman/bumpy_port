#include "video3d/scene3d.h"

#include "resources/entity_sprites.h"
#include "resources/sprite_frame.h"
#include "video/board_renderer.h"
#include "video3d/blur.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

namespace bumpy {

float scene_camera_distance() {
    const float half_fov = kCameraFovYDeg * std::numbers::pi_v<float> / 360.0f;
    return 100.0f / std::tan(half_fov);
}

SceneFrustum scene_frustum(int window_w, int window_h) {
    if (window_w <= 0 || window_h <= 0) {
        return {320.0f / 200.0f, 100.0f};  // degenerate: exact-field default
    }
    const float aspect =
        kCrtPixelAspect * static_cast<float>(window_w) / static_cast<float>(window_h);
    return {aspect, std::max(100.0f, 160.0f / aspect)};
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

}  // namespace bumpy

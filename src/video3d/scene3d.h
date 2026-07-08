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
inline constexpr float kWallBlurSigma = 1.0f;  // baked mural DOF

// CRT pixel aspect: mode 13h pixels are 240/200 = 1.2x taller than wide. The 3D
// stage always presents 4:3-corrected -- the flat path's Alt+A 4:3 look.
inline constexpr float kCrtPixelAspect = 1.2f;

// Camera distance that makes the z=0 plane subtend exactly 200 world px of height.
[[nodiscard]] float scene_camera_distance();

// Frustum shape that fills a window of any aspect with the 4:3-corrected stage:
// `aspect` is the world-unit width/height ratio for mat4_perspective and
// `half_height` the board px visible above/below centre at z=0. The 320x200
// field is always whole: windows wider than 4:3 pin half_height at 100 and
// widen, narrower windows grow half_height so all 320 px of width stay visible.
// Camera distance is unchanged either way (only the fov widens).
struct SceneFrustum {
    float aspect{};
    float half_height{};
};
[[nodiscard]] SceneFrustum scene_frustum(int window_w, int window_h);

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

// Single-channel blurred silhouette of the opaque pixels: (w+2*pad) x (h+2*pad).
[[nodiscard]] std::vector<std::uint8_t> shadow_silhouette(const MenuImage& image, int pad,
                                                          float sigma);

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

// Shadow silhouette offset and appearance.
inline constexpr float kShadowOffsetX = 5.0f;
inline constexpr float kShadowOffsetY = 7.0f;
inline constexpr float kShadowAlpha = 0.35f;
inline constexpr float kShadowBlurSigma = 2.0f;
inline constexpr int kShadowPad = 6;

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

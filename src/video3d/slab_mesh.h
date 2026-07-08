#pragma once

#include "video/menu_renderer.h"  // MenuImage
#include "video3d/scene3d.h"

#include <array>
#include <vector>

namespace bumpy {

// Per-face constant shading of an extruded slab (cheap directional light).
inline constexpr float kShadeFront = 1.0f;
inline constexpr float kShadeTop = 1.7f;  // glossy warm highlight: the top face samples the
                                          // art's top pixel row (the bar's orange edge)
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

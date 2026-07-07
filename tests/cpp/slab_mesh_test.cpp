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
    REQUIRE(top.corners[11] == bumpy::kSlabDepth);

    REQUIRE(faces[2].shade == bumpy::kShadeBottom);
    REQUIRE(faces[3].shade == bumpy::kShadeSide);
    REQUIRE(faces[4].shade == bumpy::kShadeSide);
}

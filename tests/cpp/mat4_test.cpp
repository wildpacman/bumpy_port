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

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

#include "video3d/slab_mesh.h"

namespace bumpy {

namespace {

// Board-pixel -> GL-space conversion (y flips, origin moves to the board centre).
float gx(float x) { return x - 160.0f; }
float gy(float y) { return 100.0f - y; }

QuadFace face(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2,
              float z2, float x3, float y3, float z3, float u0, float v0, float u1, float v1,
              float u2, float v2, float u3, float v3, float shade) {
    QuadFace f;
    f.corners = {x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3};
    f.uv = {u0, v0, u1, v1, u2, v2, u3, v3};
    f.shade = shade;
    return f;
}

}  // namespace

std::vector<QuadFace> quad_faces(const SceneQuad& quad, const MenuImage& frame) {
    std::vector<QuadFace> faces;
    const float fw = static_cast<float>(frame.width);
    const float fh = static_cast<float>(frame.height);

    if (quad.kind == QuadKind::billboard) {
        const float l = gx(quad.x);
        const float r = gx(quad.x + static_cast<float>(quad.w));
        const float t = gy(quad.y);
        const float b = gy(quad.y + static_cast<float>(quad.h));
        faces.push_back(face(l, t, quad.z, r, t, quad.z, r, b, quad.z, l, b, quad.z,
                             0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, kShadeFront));
        return faces;
    }

    // Slab: extrude the opaque bbox from (front - kSlabDepth) to the front plane.
    const OpaqueBounds bb = opaque_bounds(frame);
    const float x0 = quad.x + static_cast<float>(bb.x);
    const float y0 = quad.y + static_cast<float>(bb.y);
    const float x1 = x0 + static_cast<float>(bb.w);
    const float y1 = y0 + static_cast<float>(bb.h);
    const float zf = quad.z;
    const float zb = quad.z - kSlabDepth;

    const float u0 = static_cast<float>(bb.x) / fw;
    const float v0 = static_cast<float>(bb.y) / fh;
    const float u1 = static_cast<float>(bb.x + bb.w) / fw;
    const float v1 = static_cast<float>(bb.y + bb.h) / fh;
    // Edge pixel centres, for the "stretched edge pixels" side faces.
    const float v_top = (static_cast<float>(bb.y) + 0.5f) / fh;
    const float v_bot = (static_cast<float>(bb.y + bb.h) - 0.5f) / fh;
    const float u_left = (static_cast<float>(bb.x) + 0.5f) / fw;
    const float u_right = (static_cast<float>(bb.x + bb.w) - 0.5f) / fw;

    const float l = gx(x0);
    const float r = gx(x1);
    const float t = gy(y0);
    const float b = gy(y1);

    // Front.
    faces.push_back(face(l, t, zf, r, t, zf, r, b, zf, l, b, zf,
                         u0, v0, u1, v0, u1, v1, u0, v1, kShadeFront));
    // Top: back edge (z=zb) to front edge (z=zf), v pinned to the top pixel row.
    faces.push_back(face(l, t, zb, r, t, zb, r, t, zf, l, t, zf,
                         u0, v_top, u1, v_top, u1, v_top, u0, v_top, kShadeTop));
    // Bottom.
    faces.push_back(face(l, b, zf, r, b, zf, r, b, zb, l, b, zb,
                         u0, v_bot, u1, v_bot, u1, v_bot, u0, v_bot, kShadeBottom));
    // Left: u pinned to the left pixel column.
    faces.push_back(face(l, t, zb, l, t, zf, l, b, zf, l, b, zb,
                         u_left, v0, u_left, v0, u_left, v1, u_left, v1, kShadeSide));
    // Right.
    faces.push_back(face(r, t, zf, r, t, zb, r, b, zb, r, b, zf,
                         u_right, v0, u_right, v0, u_right, v1, u_right, v1, kShadeSide));
    return faces;
}

}  // namespace bumpy

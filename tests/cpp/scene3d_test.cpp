#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "resources/entity_sprites.h"
#include "resources/menu_resources.h"
#include "resources/sprite_frame.h"
#include "video3d/scene3d.h"

#include <filesystem>

namespace {

const std::filesystem::path root = ".";  // tests run from the project root

// A synthetic frame: w x h, all transparent except a filled rect.
bumpy::MenuImage make_frame(int w, int h, int rx, int ry, int rw, int rh) {
    bumpy::MenuImage img;
    img.width = w;
    img.height = h;
    img.pixels.assign(static_cast<std::size_t>(w) * h, bumpy::sprite_transparent_index);
    for (int y = ry; y < ry + rh; ++y) {
        for (int x = rx; x < rx + rw; ++x) {
            img.pixels[static_cast<std::size_t>(y) * w + x] = 3;
        }
    }
    return img;
}

}  // namespace

TEST_CASE("opaque_bounds finds the tight opaque rect") {
    const auto img = make_frame(10, 6, 2, 1, 5, 3);
    const auto b = bumpy::opaque_bounds(img);
    REQUIRE(b.any);
    REQUIRE(b.x == 2);
    REQUIRE(b.y == 1);
    REQUIRE(b.w == 5);
    REQUIRE(b.h == 3);
}

TEST_CASE("solid rectangle classifies as slab, irregular as billboard, empty as billboard") {
    REQUIRE(bumpy::classify_sprite(make_frame(10, 6, 2, 1, 5, 3)) == bumpy::QuadKind::slab);

    auto spiky = make_frame(10, 6, 2, 1, 5, 3);
    spiky.pixels[1 * 10 + 4] = bumpy::sprite_transparent_index;  // poke a hole
    REQUIRE(bumpy::classify_sprite(spiky) == bumpy::QuadKind::billboard);

    bumpy::MenuImage empty;
    empty.width = 4;
    empty.height = 4;
    empty.pixels.assign(16, bumpy::sprite_transparent_index);
    REQUIRE(bumpy::classify_sprite(empty) == bumpy::QuadKind::billboard);
}

TEST_CASE("bloom_bright_pass keeps bright pixels, zeroes dim ones, preserves hue") {
    // 3x1 row: dim (below threshold), mid (partial), bright (full).
    const std::vector<std::uint8_t> rgba = {
        50,  50,  50,  255,  // lum 0.196 < 0.30 -> zeroed
        128, 0,   0,   255,  // lum 0.502 -> partial glow, red only
        255, 255, 255, 255,  // lum 1.0 -> kept intact (f == 1)
    };
    const auto out = bumpy::bloom_bright_pass(rgba, 3, 1, 0.30f);
    REQUIRE(out.size() == rgba.size());

    // dim pixel: rgb zeroed, alpha forced opaque
    REQUIRE(out[0] == 0);
    REQUIRE(out[1] == 0);
    REQUIRE(out[2] == 0);
    REQUIRE(out[3] == 255);

    // mid pixel: partial, strictly between 0 and source; hue preserved (green/blue stay 0)
    REQUIRE(out[4] > 0);
    REQUIRE(out[4] < 128);
    REQUIRE(out[5] == 0);
    REQUIRE(out[6] == 0);

    // bright pixel: at full strength it is kept intact
    REQUIRE(out[8] == 255);
    REQUIRE(out[9] == 255);
    REQUIRE(out[10] == 255);
    REQUIRE(out[11] == 255);
}

TEST_CASE("build_live_quads mirrors the flat composition") {
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");
    bumpy::SpriteCache sprites(bank.bytes());

    bumpy::BumEntities entities{};
    entities.bytes[0 * 8 + 2] = 1;  // layer A peg at col 2 row 0 (frame 0x40, y_offset 5)

    // One spring anim on a hidden step and the ball on the hidden sentinel: neither
    // may produce a quad.
    const bumpy::ObjectAnimSprite hidden{
        .cell = 3, .frame_index = bumpy::kAnimHiddenFrame, .y_offset = 0, .layer_b = false};
    const auto quads = bumpy::build_live_quads(
        entities, {&hidden, 1}, std::nullopt, bumpy::BallPose{100, 87, 100}, sprites);

    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].frame_index == 0x40);
    REQUIRE(quads[0].x == 80.0f);
    REQUIRE(quads[0].y == 29.0f);  // 24 + y_offset 5
    const auto* img = sprites.frame(0x40);
    REQUIRE(img != nullptr);
    REQUIRE(quads[0].w == img->width);
    REQUIRE(quads[0].h == img->height);

    // Frame 0x40 (the peg bar) — pin its end-to-end classification through
    // build_live_quads: whatever classify_sprite says must be what the quad got.
    const auto expected_kind = sprites.kind(0x40);
    REQUIRE(quads[0].kind == expected_kind);
    REQUIRE(quads[0].z == (expected_kind == bumpy::QuadKind::slab ? bumpy::kSlabDepth
                                                                  : bumpy::kBillboardAbZ));
}

TEST_CASE("ball and monster quads subtract the frame origin like draw_ball/draw_monster") {
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");
    bumpy::SpriteCache sprites(bank.bytes());
    const bumpy::BumEntities entities{};

    // Any decodable bank frame works here (build_live_quads only needs size/origin);
    // 0x40 is the layer-A peg frame the entity tests already rely on.
    const int ball_frame = 0x40;
    const auto* img = sprites.frame(ball_frame);
    REQUIRE(img != nullptr);

    const auto quads = bumpy::build_live_quads(entities, {}, std::nullopt,
                                               bumpy::BallPose{ball_frame, 87, 111}, sprites);
    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].x == static_cast<float>(87 - img->origin_x));
    REQUIRE(quads[0].y == static_cast<float>(111 - img->origin_y));
    REQUIRE(quads[0].kind == bumpy::QuadKind::billboard);
    REQUIRE(quads[0].z == bumpy::kActorZ);
}

TEST_CASE("monster quad subtracts the frame origin and sits on the actor plane") {
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");
    bumpy::SpriteCache sprites(bank.bytes());
    const bumpy::BumEntities entities{};

    const int monster_frame = 0x40;  // any decodable bank frame works here
    const auto* img = sprites.frame(monster_frame);
    REQUIRE(img != nullptr);

    const auto quads = bumpy::build_live_quads(
        entities, {}, bumpy::MonsterPose{monster_frame, 120, 88},
        bumpy::BallPose{100, 0, 0},  // hidden ball: monster is the only quad
        sprites);
    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].x == static_cast<float>(120 - img->origin_x));
    REQUIRE(quads[0].y == static_cast<float>(88 - img->origin_y));
    REQUIRE(quads[0].kind == bumpy::QuadKind::billboard);
    REQUIRE(quads[0].z == bumpy::kActorZ);
}

TEST_CASE("collectibles are billboards on the collectible plane") {
    const auto bank = bumpy::decode_sprite_archive(root / "BUMSPJEU.BIN");
    bumpy::SpriteCache sprites(bank.bytes());

    bumpy::BumEntities entities{};
    entities.bytes[0x60 + 3 * 8 + 5] = 0x1b;  // layer C

    const auto quads =
        bumpy::build_live_quads(entities, {}, std::nullopt, bumpy::BallPose{100, 0, 0}, sprites);
    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].kind == bumpy::QuadKind::billboard);
    REQUIRE(quads[0].z == bumpy::kCollectibleZ);
}

TEST_CASE("build_scene3d bakes a blurred 320x200 wall with the board palette") {
    const auto level = bumpy::LevelResources::load(root, 1);
    const auto scene = bumpy::build_scene3d(level, 0, {});
    REQUIRE(scene.wall_rgba.size() == 320u * 200u * 4u);
    // Palette entry 0 is the board's own colour 0 (opaque).
    REQUIRE(scene.palette[0].a == 0xff);
}

TEST_CASE("shadow_silhouette pads and blurs the opaque mask") {
    const auto img = make_frame(4, 4, 1, 1, 2, 2);
    const int pad = 3;
    const auto mask = bumpy::shadow_silhouette(img, pad, 1.0f);
    const int w = 4 + 2 * pad;
    REQUIRE(mask.size() == static_cast<std::size_t>(w) * (4 + 2 * pad));
    // Centre of the opaque rect stays strong; a pixel just outside the silhouette
    // picked up blurred energy; the far corner stays empty.
    REQUIRE(mask[(pad + 1) * w + (pad + 1)] > 100);
    REQUIRE(mask[(pad + 0) * w + (pad + 1)] > 0);
    REQUIRE(mask[0] == 0);
}

TEST_CASE("scene_frustum: 4:3 window sees exactly the 320x200 field") {
    const auto f = bumpy::scene_frustum(800, 600);
    REQUIRE(f.aspect == Catch::Approx(1.6f));
    REQUIRE(f.half_height == Catch::Approx(100.0f));
    // width at z=0 = 2 * half_height * aspect = exactly the 320 px board
    REQUIRE(2.0f * f.half_height * f.aspect == Catch::Approx(320.0f));
}

TEST_CASE("scene_frustum: wider windows keep the field height and widen") {
    const auto f = bumpy::scene_frustum(1920, 1080);  // 16:9
    REQUIRE(f.aspect == Catch::Approx(1.2f * 1920.0f / 1080.0f));
    REQUIRE(f.half_height == Catch::Approx(100.0f));
    // reveals wall: > 320 px visible at z=0
    REQUIRE(2.0f * f.half_height * f.aspect > 320.0f);
}

TEST_CASE("scene_frustum: 16:10 default window already reveals some wall") {
    const auto f = bumpy::scene_frustum(1920, 1200);
    REQUIRE(f.aspect == Catch::Approx(1.92f));
    REQUIRE(f.half_height == Catch::Approx(100.0f));
    REQUIRE(2.0f * f.half_height * f.aspect == Catch::Approx(384.0f));
}

TEST_CASE("scene_frustum: portrait windows grow the height, field width whole") {
    const auto f = bumpy::scene_frustum(600, 800);  // 3:4
    REQUIRE(f.aspect == Catch::Approx(0.9f));
    // full 320 px width stays visible...
    REQUIRE(2.0f * f.half_height * f.aspect == Catch::Approx(320.0f));
    // ...by growing the vertical extent past the 200-line board
    REQUIRE(f.half_height > 100.0f);
}

TEST_CASE("scene_frustum: degenerate window falls back to the exact field") {
    const auto f = bumpy::scene_frustum(0, 600);
    REQUIRE(f.aspect == Catch::Approx(1.6f));
    REQUIRE(f.half_height == Catch::Approx(100.0f));
}

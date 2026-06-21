#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "game/world_map.h"
#include "resources/menu_resources.h"  // decode_sprite_archive
#include "resources/vec.h"             // decode_vec_resource
#include "video/map_renderer.h"
#include "video/screen_image.h"

// Tests run from the project root, so the originals load by bare name. render_map
// must paint the MONDE1 backdrop and blit the avatar over it at node 1.
TEST_CASE("render_map draws the avatar over the MONDE1 backdrop at node 1") {
    const auto backdrop = bumpy::decode_vec_resource("MONDE1.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto screen = backdrop.decoded_bytes();

    bumpy::WorldMap map;  // node 1, avatar at (32,32)

    bumpy::IndexedFramebuffer with_avatar(320, 200);
    const auto stats = bumpy::render_map(screen, map.view(), bank.bytes(), with_avatar);
    REQUIRE(stats.avatar_drawn);

    // Backdrop-only reference: the avatar must change at least one pixel near node 1.
    bumpy::IndexedFramebuffer backdrop_only(320, 200);
    bumpy::apply_screen_image_palette(screen, backdrop_only);
    bumpy::draw_screen_image(screen, backdrop_only);

    int differing = 0;
    for (int y = 24; y < 60; ++y) {
        for (int x = 24; x < 60; ++x) {
            if (with_avatar.pixel(x, y) != backdrop_only.pixel(x, y)) {
                ++differing;
            }
        }
    }
    REQUIRE(differing > 0);
}

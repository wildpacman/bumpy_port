#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/indexed_framebuffer.h"
#include "game/world_graphs.h"
#include "game/world_map.h"
#include "resources/menu_resources.h"  // decode_sprite_archive
#include "resources/sprite_frame.h"    // decode_sprite_frame
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

TEST_CASE("render_map draws a completed-node marker on each cleared node") {
    const auto backdrop = bumpy::decode_vec_resource("MONDE1.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto screen = backdrop.decoded_bytes();
    bumpy::WorldMap map;  // node 1

    // No cleared boards -> no markers.
    bumpy::IndexedFramebuffer none(320, 200);
    REQUIRE(bumpy::render_map(screen, map.view(), bank.bytes(), none).markers_drawn == 0);

    // Mark nodes 1 and 3 cleared (boards 0 and 2): two markers.
    std::array<std::uint8_t, 15> cleared{};
    cleared[0] = 1;  // node 1
    cleared[2] = 1;  // node 3
    bumpy::IndexedFramebuffer marked(320, 200);
    const auto stats = bumpy::render_map(screen, map.view(), bank.bytes(), marked, cleared);
    REQUIRE(stats.markers_drawn == 2);

    // Node 3 (192,32) carries no avatar, so its marker must change pixels there.
    bumpy::IndexedFramebuffer backdrop_only(320, 200);
    bumpy::apply_screen_image_palette(screen, backdrop_only);
    bumpy::draw_screen_image(screen, backdrop_only);
    const bumpy::MapNode& n3 = bumpy::world1_node(3);
    int differing = 0;
    for (int y = n3.y - 12; y < n3.y + 12; ++y) {
        for (int x = n3.x - 12; x < n3.x + 12; ++x) {
            if (marked.pixel(x, y) != backdrop_only.pixel(x, y)) {
                ++differing;
            }
        }
    }
    REQUIRE(differing > 0);
}

// The marker must sit centred on its node: the blitter centres a frame on its descriptor
// (node.x - 1, node.y) by half its dimensions (the convention that also places the avatar).
TEST_CASE("the completed-node marker is centred on its node") {
    const auto backdrop = bumpy::decode_vec_resource("MONDE1.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto screen = backdrop.decoded_bytes();
    bumpy::WorldMap map;

    std::array<std::uint8_t, 15> cleared{};
    cleared[2] = 1;  // node 3 (192,32) -- no avatar there
    bumpy::IndexedFramebuffer marked(320, 200);
    bumpy::render_map(screen, map.view(), bank.bytes(), marked, cleared);

    bumpy::IndexedFramebuffer backdrop_only(320, 200);
    bumpy::apply_screen_image_palette(screen, backdrop_only);
    bumpy::draw_screen_image(screen, backdrop_only);

    // Bounding box of the marker's changed pixels around node 3.
    const bumpy::MapNode& n3 = bumpy::world1_node(3);
    int min_x = 320, max_x = -1, min_y = 200, max_y = -1;
    for (int y = n3.y - 20; y < n3.y + 20; ++y) {
        for (int x = n3.x - 20; x < n3.x + 20; ++x) {
            if (marked.pixel(x, y) != backdrop_only.pixel(x, y)) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }
    REQUIRE(max_x >= 0);  // the marker drew something
    // The changed region's centre lands on the node descriptor (node.x - 1, node.y),
    // within a couple of pixels of slack for the marker art's own asymmetry.
    REQUIRE(std::abs((min_x + max_x) / 2 - (n3.x - 1)) <= 3);
    REQUIRE(std::abs((min_y + max_y) / 2 - n3.y) <= 3);
}

TEST_CASE("completed-node markers follow the current world's node positions") {
    const auto backdrop = bumpy::decode_vec_resource("MONDE2.VEC");
    const auto bank = bumpy::decode_sprite_archive("BUMSPJEU.BIN");
    const auto screen = backdrop.decoded_bytes();

    bumpy::WorldMap map(2);  // world 2, node 1 at (112,32)

    std::vector<std::uint8_t> cleared(static_cast<std::size_t>(bumpy::world_node_count(2)), 0);
    cleared[0] = 1;  // board 0 = node 1 cleared
    bumpy::IndexedFramebuffer marked(320, 200);
    const auto stats = bumpy::render_map(screen, map.view(), bank.bytes(), marked, cleared);
    REQUIRE(stats.markers_drawn == 1);

    // The marker changed pixels around world-2 node 1 (112,32).
    bumpy::IndexedFramebuffer backdrop_only(320, 200);
    bumpy::apply_screen_image_palette(screen, backdrop_only);
    bumpy::draw_screen_image(screen, backdrop_only);
    const bumpy::MapNode& n1 = bumpy::world_node(2, 1);
    int differing = 0;
    for (int y = n1.y - 12; y < n1.y + 12; ++y) {
        for (int x = n1.x - 12; x < n1.x + 12; ++x) {
            if (marked.pixel(x, y) != backdrop_only.pixel(x, y)) ++differing;
        }
    }
    REQUIRE(differing > 0);
}

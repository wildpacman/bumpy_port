#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "resources/level_resources.h"
#include "resources/vec.h"
#include "video/board_renderer.h"

#include <cstdint>
#include <tuple>
#include <vector>
#include "resources/entity_sprites.h"

// Tests run from the project root, so the original level files load by name.
// Board 0 of level 1 fills all 260 cells of its 20x13 grid: 248 single objects
// and 12 stacked markers (0xf8), confirmed from the decoded D1.DEC grid. The
// counts lock in the cell addressing and the single/stacked split.
TEST_CASE("rendering level 1 board 0 stamps every grid cell") {
    const auto level = bumpy::LevelResources::load(".", 1);
    const auto backdrop = bumpy::decode_vec_resource("MONDE1.VEC");
    bumpy::IndexedFramebuffer frame(320, 200);

    const auto stats = bumpy::render_board(level, 0, backdrop.decoded_bytes(), frame);

    REQUIRE(stats.objects_drawn == 248);
    REQUIRE(stats.stacked_cells == 12);
    REQUIRE(stats.objects_drawn + stats.stacked_cells ==
            bumpy::LevelBoard::columns * bumpy::LevelBoard::rows);
    REQUIRE(stats.stacked_tiles > 0);  // stacked markers stamp at least one tile each

    // The objects paint real colour over the cleared field.
    bool any_painted = false;
    for (const auto pixel : frame.pixels()) {
        if (pixel != 0) {
            any_painted = true;
            break;
        }
    }
    REQUIRE(any_painted);
}

TEST_CASE("for_each_entity_sprite yields faithful frames and positions") {
    bumpy::BumEntities bum{};
    bum.bytes[0 * 8 + 2] = 1;                                       // layer A, col 2 row 0: peg
    bum.bytes[bumpy::BumEntities::layer_b_offset + 1 * 8 + 7] = 1;  // layer B col 7: never drawn
    bum.bytes[0x60 + 3 * 8 + 5] = 0x1b;                             // layer C, col 5 row 3

    std::vector<std::tuple<bumpy::EntityLayer, int, int, int>> seen;
    bumpy::for_each_entity_sprite(bum, [&](bumpy::EntityLayer layer, int frame, int x, int y) {
        seen.emplace_back(layer, frame, x, y);
    });

    REQUIRE(seen.size() == 2);
    // Layer A code 1 -> frame 0x40, y_offset 5 at DS:0xf4 slot (col*40, 24+row*32).
    REQUIRE(std::get<0>(seen[0]) == bumpy::EntityLayer::a);
    REQUIRE(std::get<1>(seen[0]) == 0x40);
    REQUIRE(std::get<2>(seen[0]) == 80);
    REQUIRE(std::get<3>(seen[0]) == 24 + 5);
    // Layer C value 0x1b -> frame 0x194 at the DS:0x274 cell position.
    const auto cpos = bumpy::bum_cell_position(5, 3);
    REQUIRE(std::get<0>(seen[1]) == bumpy::EntityLayer::c);
    REQUIRE(std::get<1>(seen[1]) == 0x194);
    REQUIRE(std::get<2>(seen[1]) == cpos.x);
    REQUIRE(std::get<3>(seen[1]) == cpos.y);
}

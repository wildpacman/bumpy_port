#include <catch2/catch_test_macros.hpp>

#include "core/indexed_framebuffer.h"
#include "resources/level_resources.h"
#include "resources/vec.h"
#include "video/board_renderer.h"

#include <cstdint>

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

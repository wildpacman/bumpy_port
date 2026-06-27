#include <catch2/catch_test_macros.hpp>

#include "game/world_graphs.h"
#include "resources/world_resources.h"

// Tests run from the project root, so the originals load by bare name (root ".").
TEST_CASE("each world's board count matches its node count and has a backdrop") {
    for (int w = 1; w <= bumpy::kWorldCount; ++w) {
        const auto res = bumpy::WorldResources::load(".", w);
        REQUIRE(res.world() == w);
        REQUIRE_FALSE(res.backdrop().empty());
        // The DEC tile-board count must equal the map node count: map node N selects
        // board N-1, and clearing a world clears every node, so they must correspond.
        REQUIRE(res.board_count() == static_cast<std::size_t>(bumpy::world_node_count(w)));
    }
}

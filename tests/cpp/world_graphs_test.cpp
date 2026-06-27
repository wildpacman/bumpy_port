#include <catch2/catch_test_macros.hpp>

#include "game/world_graphs.h"
#include "game/world_map.h"  // MapNode

#include <set>
#include <vector>

using bumpy::world_node;
using bumpy::world_node_count;

TEST_CASE("generated world-1 graph reproduces the historical kWorld1") {
    REQUIRE(world_node_count(1) == 15);
    // {up, down, left, right, x, y} for nodes 1..15 (was src/game/world_map.cpp kWorld1).
    const bumpy::MapNode want[15] = {
        {0, 0, 0, 2, 32, 32},    {0, 9, 1, 0, 112, 32},   {0, 0, 0, 4, 192, 32},
        {0, 7, 3, 0, 272, 32},   {0, 8, 0, 0, 32, 80},    {0, 10, 0, 7, 192, 80},
        {4, 0, 6, 0, 272, 80},   {5, 12, 0, 9, 32, 128},  {2, 0, 8, 0, 112, 128},
        {6, 0, 0, 11, 192, 128}, {0, 15, 10, 0, 272, 128}, {8, 0, 0, 13, 32, 176},
        {0, 0, 12, 14, 112, 176}, {0, 0, 13, 15, 192, 176}, {11, 0, 14, 0, 272, 176},
    };
    for (int node = 1; node <= 15; ++node) {
        const bumpy::MapNode& got = world_node(1, node);
        const bumpy::MapNode& w = want[node - 1];
        REQUIRE(got.up == w.up);
        REQUIRE(got.down == w.down);
        REQUIRE(got.left == w.left);
        REQUIRE(got.right == w.right);
        REQUIRE(got.x == w.x);
        REQUIRE(got.y == w.y);
    }
}

TEST_CASE("every world graph is sane, connected, and symmetric") {
    for (int world = 1; world <= bumpy::kWorldCount; ++world) {
        const int count = world_node_count(world);
        REQUIRE(count >= 1);
        REQUIRE(world_node(world, 0).right == 0);  // node 0 is the zero sentinel

        for (int n = 1; n <= count; ++n) {
            const bumpy::MapNode& node = world_node(world, n);
            REQUIRE(node.x >= 0);
            REQUIRE(node.x < 320);
            REQUIRE(node.y >= 0);
            REQUIRE(node.y < 200);
            for (int nb : {int(node.up), int(node.down), int(node.left), int(node.right)}) {
                REQUIRE(nb >= 0);
                REQUIRE(nb <= count);  // links reference valid nodes (0 = no link)
            }
        }

        // Symmetric links: my up <-> their down, my left <-> their right.
        for (int n = 1; n <= count; ++n) {
            const bumpy::MapNode& node = world_node(world, n);
            if (node.up != 0) REQUIRE(world_node(world, node.up).down == n);
            if (node.down != 0) REQUIRE(world_node(world, node.down).up == n);
            if (node.left != 0) REQUIRE(world_node(world, node.left).right == n);
            if (node.right != 0) REQUIRE(world_node(world, node.right).left == n);
        }

        // Every node reachable from node 1 (clearing a world requires clearing all nodes).
        std::set<int> seen{1};
        std::vector<int> stack{1};
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            const bumpy::MapNode& node = world_node(world, cur);
            for (int nb : {int(node.up), int(node.down), int(node.left), int(node.right)}) {
                if (nb != 0 && seen.insert(nb).second) stack.push_back(nb);
            }
        }
        REQUIRE(static_cast<int>(seen.size()) == count);
    }
}

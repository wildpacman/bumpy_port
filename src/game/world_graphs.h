#pragma once

#include "game/world_map.h"  // MapNode

namespace bumpy {

// Number of worlds (MONDE1..MONDE9 / D1..D9).
inline constexpr int kWorldCount = 9;

// Per-world map node graph + positions, baked from BUMPY.UNPACKED.EXE by
// tools/re/dump_world_graphs.py (graph far-ptr table DS:0x10c8, positions DS:0x10ec,
// world W pointer at table + W*4). Node 0 is a zero sentinel; nodes 1..count are real.
// world in 1..kWorldCount, node in 0..world_node_count(world).
[[nodiscard]] const MapNode& world_node(int world, int node);
[[nodiscard]] int world_node_count(int world);  // 12 or 15

}  // namespace bumpy

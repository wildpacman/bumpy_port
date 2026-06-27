#pragma once

#include "resources/level_resources.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace bumpy {

// All per-world *disk* resources: the D{n} level data and the decoded MONDE{n}.VEC
// backdrop (which also carries the world's map palette at offset 0x33). World-independent
// assets (the BUMSPJEU sprite bank, the DDFNT2 font) are loaded once by the caller, not
// here. The SDL shell owns one of these and reloads it when App requests a new world.
class WorldResources {
public:
    static WorldResources load(const std::filesystem::path& root, int world);

    [[nodiscard]] int world() const noexcept { return world_; }
    [[nodiscard]] const LevelResources& level() const noexcept { return level_; }
    // Decoded 320x200 screen-format MONDE{n}.VEC bytes (the span views owned storage).
    [[nodiscard]] std::span<const std::uint8_t> backdrop() const noexcept { return backdrop_; }
    // Number of completable boards = number of map nodes (the DEC tile-board count).
    [[nodiscard]] std::size_t board_count() const noexcept { return level_.board_count(); }

private:
    WorldResources(int world, LevelResources level, std::vector<std::uint8_t> backdrop)
        : world_(world), level_(std::move(level)), backdrop_(std::move(backdrop)) {}

    int world_{};
    LevelResources level_;
    std::vector<std::uint8_t> backdrop_;  // owned decoded MONDE{n}.VEC; backdrop() views it
};

}  // namespace bumpy

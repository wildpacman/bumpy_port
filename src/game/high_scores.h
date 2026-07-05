#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace bumpy {

// One high-score record. In the original these are 7 x 8-byte records at DS:0x8f0
// (name pointer + 32-bit score); the port keeps the displayed name inline. Name
// chars are the glyph alphabet {'.', '0'-'9', 'A'-'Z'} ('.' = blank/pad).
struct HighScoreEntry {
    std::array<char, 8> name;
    std::uint32_t score;
};

inline constexpr std::size_t kHighScoreCount = 7;
inline constexpr std::size_t kHighScoreNameLength = 8;

// The high-score table for one session. Seeded with the 7 baked defaults recovered
// from BUMPY.UNPACKED.EXE (DS:0x8f0, names at DS:0x11e6+); no disk persistence, so it
// resets to the defaults every launch, exactly like the original's data-segment table.
// FUN_1000_57e1 (draw + insert test) / FUN_1000_59d3 (name entry).
class HighScoreTable {
public:
    HighScoreTable() noexcept;

    [[nodiscard]] const std::array<HighScoreEntry, kHighScoreCount>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] const HighScoreEntry& entry(std::size_t row) const noexcept { return entries_[row]; }
    [[nodiscard]] HighScoreEntry& entry(std::size_t row) noexcept { return entries_[row]; }

    // The first row whose score `score` strictly beats, or -1 if it beats none.
    [[nodiscard]] int qualifies(std::uint32_t score) const noexcept;

    // Insert `score`: shift the rows below down one, drop the last, seed the new row's
    // name to 8x'A' (FUN_1000_57e1), and return the inserted row -- or -1 if it did not
    // qualify (no change).
    int insert(std::uint32_t score) noexcept;

private:
    std::array<HighScoreEntry, kHighScoreCount> entries_;
};

}  // namespace bumpy

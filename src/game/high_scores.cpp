#include "game/high_scores.h"

namespace bumpy {

namespace {
// One 8-char name, padded from a literal (defaults are all exactly 8 chars).
constexpr std::array<char, 8> name8(const char (&s)[9]) {
    return {s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]};
}
}  // namespace

HighScoreTable::HighScoreTable() noexcept
    // Baked defaults, verified from BUMPY.UNPACKED.EXE (records at DS:0x8f0, file 0x11D30;
    // names in the DS string pool at 0x11e6+). Descending by score.
    : entries_{{
          {name8("BIG JIM."), 5000000},
          {name8("SUPER JO"), 3000000},
          {name8("STEVE..."), 1000000},
          {name8("WILIAM.."), 200000},
          {name8("JOHNNY.."), 30000},
          {name8("FRANK..."), 4000},
          {name8("MIKE...."), 500},
      }} {}

int HighScoreTable::qualifies(std::uint32_t score) const noexcept {
    for (std::size_t row = 0; row < kHighScoreCount; ++row) {
        if (score > entries_[row].score) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

int HighScoreTable::insert(std::uint32_t score) noexcept {
    const int row = qualifies(score);
    if (row < 0) {
        return -1;
    }
    for (int r = static_cast<int>(kHighScoreCount) - 1; r > row; --r) {
        entries_[static_cast<std::size_t>(r)] = entries_[static_cast<std::size_t>(r - 1)];
    }
    entries_[static_cast<std::size_t>(row)].name.fill('A');
    entries_[static_cast<std::size_t>(row)].score = score;
    return row;
}

}  // namespace bumpy

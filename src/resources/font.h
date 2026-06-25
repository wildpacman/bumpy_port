#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace bumpy {

// One glyph of a DDFNT2.CAR bitmap font: a monochrome (1bpp) bitmap, row-major,
// MSB-first, `bytes_per_row = ceil(width/8)` bytes per row, `height` rows. `width`
// is the visible pixel width; `y_offset` drops the bitmap below the glyph top.
struct FontGlyph {
    int width{};
    int height{};
    int y_offset{};
    int bytes_per_row{};
    std::span<const std::uint8_t> bitmap;  // height * bytes_per_row bytes; empty if blank
};

// The original's bitmap font (DDFNT2.CAR), used to draw the HUD score
// (FUN_1000_0816 -> FUN_1ab9_13ec -> the VGA glyph rasterizer FUN_1ab9_1607).
// Format recovered in analysis/specs/screen-flow.md ("HUD score font"):
//   header: [0]=first_char, [1]=last_char(exclusive), [2]=ascent, [3]=line metric,
//           [4]=inter-char spacing, [5]=reserved, [6..]=BE16 per-char record offsets
//           (relative to the font base), one per char in [first, last).
//   glyph record: [0]=width, [1]=height, [2]=y_offset, [3..]=bitmap.
// The file is read raw (no VEC container).
class Font {
public:
    static Font load(const std::filesystem::path& path);
    static Font from_bytes(std::vector<std::uint8_t> bytes);

    [[nodiscard]] int first_char() const noexcept { return first_; }
    [[nodiscard]] int last_char() const noexcept { return last_; }  // exclusive
    [[nodiscard]] int ascent() const noexcept { return ascent_; }   // glyph top = baseline - ascent
    [[nodiscard]] int spacing() const noexcept { return spacing_; } // x_advance = width + spacing

    // The glyph for `ch`. An out-of-range character returns an empty glyph (width 0):
    // the original's per-char draw (FUN_1ab9_13bc) range-checks and skips such chars.
    [[nodiscard]] FontGlyph glyph(unsigned char ch) const;

private:
    explicit Font(std::vector<std::uint8_t> bytes);

    std::vector<std::uint8_t> bytes_;
    int first_{};
    int last_{};
    int ascent_{};
    int spacing_{};
};

}  // namespace bumpy

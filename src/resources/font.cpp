#include "resources/font.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace bumpy {
namespace {

constexpr std::size_t header_size = 6;

}  // namespace

Font::Font(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    if (bytes_.size() < header_size) {
        throw std::runtime_error("font: file too small for a DDFNT2 header");
    }
    first_ = bytes_[0];
    last_ = bytes_[1];
    ascent_ = bytes_[2];
    spacing_ = bytes_[4];
    if (last_ <= first_) {
        throw std::runtime_error("font: empty character range");
    }
    // The BE16 offset table must fit: one entry per char in [first, last).
    const std::size_t table_end = header_size + static_cast<std::size_t>(last_ - first_) * 2;
    if (table_end > bytes_.size()) {
        throw std::runtime_error("font: offset table runs past the file");
    }
}

Font Font::from_bytes(std::vector<std::uint8_t> bytes) {
    return Font(std::move(bytes));
}

Font Font::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("font: cannot open " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    return Font(std::move(bytes));
}

FontGlyph Font::glyph(unsigned char ch) const {
    if (ch < first_ || ch >= last_) {
        return FontGlyph{};  // out of range: the original skips it (no draw, no advance)
    }
    const std::size_t entry = header_size + static_cast<std::size_t>(ch - first_) * 2;
    const std::size_t rec =
        (static_cast<std::size_t>(bytes_[entry]) << 8) | bytes_[entry + 1];  // BE16, base-relative
    if (rec + 3 > bytes_.size()) {
        return FontGlyph{};
    }
    const int width = bytes_[rec];
    const int height = bytes_[rec + 1];
    const int y_offset = bytes_[rec + 2];
    const int bytes_per_row = width > 0 ? (width - 1) / 8 + 1 : 0;
    const std::size_t bitmap_bytes = static_cast<std::size_t>(bytes_per_row) * height;
    if (rec + 3 + bitmap_bytes > bytes_.size()) {
        return FontGlyph{};
    }
    return FontGlyph{
        width,
        height,
        y_offset,
        bytes_per_row,
        std::span<const std::uint8_t>(bytes_.data() + rec + 3, bitmap_bytes),
    };
}

}  // namespace bumpy

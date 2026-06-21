#include "resources/level_resources.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open level resource: " + path.string());
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::filesystem::path level_file(const std::filesystem::path& root, int level_number, const char* extension) {
    return root / ("D" + std::to_string(level_number) + extension);
}

}  // namespace

namespace bumpy {

std::span<const std::uint8_t> LevelBoard::cell(int col, int row) const {
    if (col < 0 || col >= columns || row < 0 || row >= rows) {
        throw std::out_of_range("level board cell is outside the 20x13 grid");
    }
    const auto offset = static_cast<std::size_t>(grid_offset + col * column_stride + row * cell_bytes);
    return std::span<const std::uint8_t>(bytes.data() + offset, cell_bytes);
}

std::uint8_t LevelBoard::object_index(int col, int row) const {
    return cell(col, row)[0];
}

namespace {

// Shared bounds-checked lookup for a BUM layer: cell = row*8 + col, offset into
// the 194-byte record by the layer base. Matches FUN_1000_2a78's indexing.
std::uint8_t bum_layer_cell(const std::array<std::uint8_t, BumEntities::record_size>& bytes,
                            std::size_t layer_offset, int col, int row) {
    if (col < 0 || col >= BumEntities::columns || row < 0 || row >= BumEntities::rows) {
        throw std::out_of_range("BUM entity cell is outside the 8x6 grid");
    }
    return bytes[layer_offset + static_cast<std::size_t>(row) * BumEntities::columns + col];
}

}  // namespace

std::uint8_t BumEntities::layer_a(int col, int row) const {
    return bum_layer_cell(bytes, layer_a_offset, col, row);
}

std::uint8_t BumEntities::layer_b(int col, int row) const {
    return bum_layer_cell(bytes, layer_b_offset, col, row);
}

std::uint8_t BumEntities::layer_c(int col, int row) const {
    return bum_layer_cell(bytes, layer_c_offset, col, row);
}

std::uint8_t BumEntities::param(int index) const {
    if (index < 0 || index >= param_count) {
        throw std::out_of_range("BUM board param index is outside 0..5");
    }
    return bytes[params_offset + static_cast<std::size_t>(index)];
}

CellPosition bum_cell_position(int col, int row) {
    if (col < 0 || col >= BumEntities::columns || row < 0 || row >= BumEntities::rows) {
        throw std::out_of_range("BUM cell position is outside the 8x6 grid");
    }
    // Faithful to the DS:0x274 coordinate table: columns 0..6 at 8 + col*40, the
    // spare column 7 at 32, rows at 8 + row*32.
    const int x = (col == 7) ? 32 : 8 + col * 40;
    const int y = 8 + row * 32;
    return CellPosition{x, y};
}

MenuImage deplane_object_sheet(std::span<const std::uint8_t> pav_body) {
    constexpr int width = LevelResources::sheet_width;
    constexpr int height = LevelResources::sheet_height;
    constexpr std::size_t plane_size = static_cast<std::size_t>(width) * height / 8;  // 7680
    if (pav_body.size() < 4 * plane_size) {
        throw std::runtime_error("D?.PAV body is shorter than four 320x192 bit-planes");
    }
    MenuImage image{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height)};
    for (std::size_t pixel = 0; pixel < image.pixels.size(); ++pixel) {
        const std::size_t byte = pixel >> 3U;
        const unsigned shift = 7U - static_cast<unsigned>(pixel & 7U);
        std::uint8_t value = 0;
        for (int plane = 0; plane < 4; ++plane) {
            const std::uint8_t bit = (pav_body[plane * plane_size + byte] >> shift) & 1U;
            value = static_cast<std::uint8_t>(value | (bit << plane));
        }
        image.pixels[pixel] = value;
    }
    return image;
}

const LevelBoard& LevelResources::board(std::size_t index) const {
    if (index >= boards_.size()) {
        throw std::out_of_range("level board index is outside the decoded DEC");
    }
    return boards_[index];
}

const MenuImage& LevelResources::object_sheet() const {
    if (!object_sheet_) {
        throw std::runtime_error("this level ships no D?.PAV object sheet");
    }
    return *object_sheet_;
}

std::span<const std::uint8_t> LevelResources::bum_board(std::size_t index) const {
    constexpr std::size_t record = 0xc2;  // 194
    if (index >= bum_board_count_) {
        throw std::out_of_range("BUM board index is outside the decoded BUM");
    }
    return std::span<const std::uint8_t>(bum_.data() + 2 + index * record, record);
}

BumEntities LevelResources::bum_entities(std::size_t index) const {
    const auto record = bum_board(index);  // bounds-checked, 194 bytes
    BumEntities entities;
    std::copy(record.begin(), record.end(), entities.bytes.begin());
    return entities;
}

LevelResources LevelResources::load(const std::filesystem::path& root, int level_number) {
    LevelResources level;
    level.level_number_ = level_number;

    // D?.PAV -- 6-byte header + 320x192 object sheet. D3 ships an empty PAV.
    const auto pav_path = level_file(root, level_number, ".PAV");
    if (std::filesystem::exists(pav_path) && std::filesystem::file_size(pav_path) > 0) {
        const auto pav_resource = decode_vec_resource(pav_path);
        const auto pav = pav_resource.decoded_bytes();
        if (pav.size() < sheet_header + 4 * (static_cast<std::size_t>(sheet_width) * sheet_height / 8)) {
            throw std::runtime_error("decoded D?.PAV is too small for a 320x192 object sheet");
        }
        level.object_sheet_ =
            deplane_object_sheet(pav.subspan(sheet_header, pav.size() - sheet_header));
    }

    // D?.DEC -- 2-byte header + N records of 812 bytes.
    const auto dec_resource = decode_vec_resource(level_file(root, level_number, ".DEC"));
    const auto dec = dec_resource.decoded_bytes();
    if (dec.size() < 2 || (dec.size() - 2) % LevelBoard::record_size != 0) {
        throw std::runtime_error("decoded D?.DEC size is not 2 + N*812");
    }
    const std::size_t dec_boards = (dec.size() - 2) / LevelBoard::record_size;
    level.boards_.resize(dec_boards);
    for (std::size_t i = 0; i < dec_boards; ++i) {
        const auto* record = dec.data() + 2 + i * LevelBoard::record_size;
        std::copy(record, record + LevelBoard::record_size, level.boards_[i].bytes.begin());
    }

    // D?.BUM -- 2-byte header + N records of 194 bytes. D6/D9 ship raw (already
    // decoded), so a VEC-decode failure means treat the file bytes as the BUM.
    const auto bum_path = level_file(root, level_number, ".BUM");
    try {
        const auto bum_resource = decode_vec_resource(bum_path);
        const auto decoded = bum_resource.decoded_bytes();
        level.bum_.assign(decoded.begin(), decoded.end());
        level.bum_was_raw_ = false;
    } catch (const std::runtime_error&) {
        level.bum_ = read_file(bum_path);
        level.bum_was_raw_ = true;
    }
    constexpr std::size_t bum_record = 0xc2;  // 194
    if (level.bum_.size() < 2 || (level.bum_.size() - 2) % bum_record != 0) {
        throw std::runtime_error("decoded D?.BUM size is not 2 + N*194");
    }
    level.bum_board_count_ = (level.bum_.size() - 2) / bum_record;
    // Note: DEC and BUM board counts usually match, but not always -- D7 has 12
    // DEC tile-boards yet 15 BUM entity-boards (verified by file size). So the two
    // counts are kept independent rather than cross-validated.
    return level;
}

}  // namespace bumpy

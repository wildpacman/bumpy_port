#include "resources/sprite_frame.h"

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t frame_header_size = 12;
constexpr std::size_t relocated_resource_base = 0x800;
constexpr int sprite_plane_count = 4;
constexpr int pixels_per_width_unit = 4;
constexpr int pixels_per_plane_byte = 8;

[[noreturn]] void fail_sprite(std::size_t offset, const char* message) {
    std::ostringstream output;
    output << "sprite frame: " << message << " at 0x" << std::hex << offset;
    throw std::runtime_error(output.str());
}

void require_range(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        fail_sprite(offset, "read outside archive");
    }
}

std::uint16_t read_be_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(bytes, offset, 2);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1]);
}

std::uint32_t read_be_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(bytes, offset, 4);
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::size_t frame_data_offset(std::span<const std::uint8_t> bytes, int frame_index) {
    const auto pointer_offset = static_cast<std::size_t>(frame_index) * 4U;
    const auto raw_offset = static_cast<std::size_t>(read_be_u32(bytes, pointer_offset));
    if (raw_offset == 0) {
        fail_sprite(pointer_offset, "missing frame pointer");
    }
    const auto resolved_offset = relocated_resource_base + raw_offset;
    if (resolved_offset < frame_header_size) {
        fail_sprite(pointer_offset, "frame pointer precedes its header");
    }
    require_range(bytes, resolved_offset - frame_header_size, frame_header_size);
    return resolved_offset;
}

struct SpriteHeader {
    std::uint16_t mask_count;
    std::uint16_t flags;
    std::uint16_t origin_x;
    std::uint16_t origin_y;
    std::uint16_t width_units;
    std::uint16_t height;
};

SpriteHeader read_header(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(bytes, offset, frame_header_size);
    return SpriteHeader{
        read_be_u16(bytes, offset),
        read_be_u16(bytes, offset + 2),
        read_be_u16(bytes, offset + 4),
        read_be_u16(bytes, offset + 6),
        read_be_u16(bytes, offset + 8),
        read_be_u16(bytes, offset + 10),
    };
}

std::size_t pixel_data_size(const SpriteHeader& header, std::size_t offset) {
    if ((header.flags & 0x00c0U) != 0) {
        fail_sprite(offset + 2, "compressed sprite records are not used by the menu marker decoder");
    }
    if (header.width_units == 0 || header.height == 0 || (header.width_units % 4U) != 0) {
        fail_sprite(offset + 8, "invalid sprite dimensions");
    }
    return static_cast<std::size_t>(header.width_units) * static_cast<std::size_t>(header.height) * 2U;
}

}  // namespace

namespace bumpy {

MenuImage decode_sprite_frame(std::span<const std::uint8_t> archive_data, int frame_index) {
    if (frame_index < 0) {
        throw std::runtime_error("sprite frame: negative frame index");
    }

    const auto data_offset = frame_data_offset(archive_data, frame_index);
    const auto frame_offset = data_offset - frame_header_size;
    const auto header = read_header(archive_data, frame_offset);
    const auto data_size = pixel_data_size(header, frame_offset);
    require_range(archive_data, data_offset, data_size);

    const int width = static_cast<int>(header.width_units) * pixels_per_width_unit;
    const int height = static_cast<int>(header.height);
    const std::size_t plane_bytes_per_row = static_cast<std::size_t>(width) / pixels_per_plane_byte;
    const std::size_t row_stride = plane_bytes_per_row * sprite_plane_count;

    MenuImage image{
        width,
        height,
        std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)),
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t color = 0;
            for (int plane = 0; plane < sprite_plane_count; ++plane) {
                const auto byte_offset = data_offset + static_cast<std::size_t>(y) * row_stride +
                                         static_cast<std::size_t>(plane) * plane_bytes_per_row +
                                         static_cast<std::size_t>(x / pixels_per_plane_byte);
                const auto bit = static_cast<std::uint8_t>(
                    (archive_data[byte_offset] >> (7U - static_cast<unsigned>(x % pixels_per_plane_byte))) & 1U);
                color = static_cast<std::uint8_t>(color | (bit << plane));
            }
            image.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                color == 0 ? sprite_transparent_index : color;
        }
    }

    return image;
}

}  // namespace bumpy

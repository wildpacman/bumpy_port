#include "resources/vec.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::size_t header_size = 12;

std::string resource_name_from_path(const std::filesystem::path& path) {
    auto name = path.filename().string();
    if (name.empty()) {
        name = path.string();
    }
    return name;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open resource: " + path.string());
    }
    std::vector<std::uint8_t> data;
    char byte{};
    while (input.get(byte)) {
        data.push_back(static_cast<std::uint8_t>(byte));
    }
    if (!input.eof()) {
        throw std::runtime_error("cannot read resource: " + path.string());
    }
    return data;
}

std::string hex_offset(std::size_t offset) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setfill('0') << std::setw(8) << offset;
    return output.str();
}

[[noreturn]] void fail_at(const std::string& resource_name, std::size_t offset, const std::string& message) {
    throw std::runtime_error(resource_name + ": " + message + " at " + hex_offset(offset));
}

void require_range(
    std::span<const std::uint8_t> bytes,
    const std::string& resource_name,
    std::size_t offset,
    std::size_t count) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        fail_at(resource_name, offset, "read outside resource");
    }
}

std::uint16_t read_be_u16(std::span<const std::uint8_t> bytes, const std::string& resource_name, std::size_t offset) {
    require_range(bytes, resource_name, offset, 2);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                      static_cast<std::uint16_t>(bytes[offset + 1]));
}

std::uint32_t read_be_u32(std::span<const std::uint8_t> bytes, const std::string& resource_name, std::size_t offset) {
    require_range(bytes, resource_name, offset, 4);
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

struct ParsedHeader {
    std::uint32_t output_size{};
    std::uint32_t auxiliary{};
    std::uint16_t method_flags{};
    std::uint16_t checksum{};
    std::uint8_t method{};
    bool final_layer{};
};

ParsedHeader parse_header(std::span<const std::uint8_t> layer, const std::string& resource_name) {
    require_range(layer, resource_name, 0, header_size);

    const auto high_size = read_be_u16(layer, resource_name, 0);
    if (high_size > 0x000fU) {
        fail_at(resource_name, 0, "VEC decoded size high word exceeds original limit");
    }

    const std::array<std::uint16_t, 6> words{
        high_size,
        read_be_u16(layer, resource_name, 2),
        read_be_u16(layer, resource_name, 4),
        read_be_u16(layer, resource_name, 6),
        read_be_u16(layer, resource_name, 8),
        read_be_u16(layer, resource_name, 10),
    };
    const auto checksum = static_cast<std::uint16_t>(words[0] ^ words[1] ^ words[2] ^ words[3] ^ words[4]);
    if (checksum != words[5]) {
        fail_at(resource_name, 10, "VEC header XOR checksum mismatch");
    }
    if ((words[4] & 0x7f00U) != 0) {
        fail_at(resource_name, 8, "VEC method flags contain reserved bits");
    }

    const auto method = static_cast<std::uint8_t>(words[4] & 0x003fU);
    if (method < 1 || method > 30) {
        fail_at(resource_name, 8, "VEC method is outside the original dispatch table");
    }

    return ParsedHeader{
        read_be_u32(layer, resource_name, 0),
        read_be_u32(layer, resource_name, 4),
        words[4],
        words[5],
        method,
        (words[4] & 0x8000U) != 0,
    };
}

std::vector<std::uint8_t> decode_method_4(
    std::span<const std::uint8_t> payload,
    const std::string& resource_name,
    std::uint32_t output_size,
    bumpy::VecLayer& layer) {
    if (payload.empty()) {
        fail_at(resource_name, header_size, "VEC method 4 payload is empty");
    }

    const auto marker = payload[0];
    layer.command = bumpy::VecMethod4{marker, payload.size()};

    std::vector<std::uint8_t> output;
    output.reserve(output_size);
    std::size_t offset = 1;
    while (offset < payload.size()) {
        auto value = payload[offset++];
        if (value != marker) {
            output.push_back(value);
            continue;
        }
        if (offset >= payload.size()) {
            fail_at(resource_name, header_size + offset, "VEC method 4 marker lacks a value");
        }
        value = payload[offset++];
        if (value == marker) {
            output.push_back(marker);
            continue;
        }
        if (offset >= payload.size()) {
            fail_at(resource_name, header_size + offset, "VEC method 4 run lacks a count");
        }
        const auto count = payload[offset++];
        output.insert(output.end(), count == 0 ? 256 : count, value);
    }

    return output;
}

std::vector<std::uint8_t> decode_method_12(
    std::span<const std::uint8_t> payload,
    const std::string& resource_name,
    std::uint32_t output_size,
    bumpy::VecLayer& layer) {
    require_range(payload, resource_name, 0, 2);
    const auto marker = static_cast<std::uint16_t>((static_cast<std::uint16_t>(payload[0]) << 8U) | payload[1]);
    const auto marker_byte = static_cast<std::uint8_t>(marker & 0x00ffU);
    const auto block_count = static_cast<std::size_t>((output_size + 31U) / 32U);
    constexpr std::size_t marker_size = 2;
    if (block_count > (payload.size() - marker_size) / 4U) {
        fail_at(resource_name, header_size + marker_size, "VEC method 12 mask stream is truncated");
    }

    const auto mask_end = marker_size + block_count * 4U;
    const auto literal_count = payload.size() - mask_end;
    layer.command = bumpy::VecMethod12{marker, block_count, literal_count, payload.size()};

    std::vector<std::uint8_t> output;
    output.reserve(output_size);
    std::size_t literal_offset = 0;
    for (std::size_t block = 0; block < block_count; ++block) {
        const auto mask_offset = marker_size + block * 4U;
        const auto mask = (static_cast<std::uint32_t>(payload[mask_offset]) << 24U) |
                          (static_cast<std::uint32_t>(payload[mask_offset + 1]) << 16U) |
                          (static_cast<std::uint32_t>(payload[mask_offset + 2]) << 8U) |
                          static_cast<std::uint32_t>(payload[mask_offset + 3]);
        for (int bit = 31; bit >= 0; --bit) {
            if (output.size() == output_size) {
                break;
            }
            if ((mask & (1UL << bit)) != 0) {
                output.push_back(marker_byte);
            } else {
                if (literal_offset == literal_count) {
                    fail_at(resource_name, header_size + mask_end + literal_offset, "VEC method 12 literal stream is truncated");
                }
                output.push_back(payload[mask_end + literal_offset]);
                ++literal_offset;
            }
        }
    }

    if (literal_offset != literal_count) {
        fail_at(resource_name, header_size + mask_end + literal_offset, "VEC method 12 literal stream has trailing bytes");
    }
    return output;
}

}  // namespace

namespace bumpy {

VecResource::VecResource(
    std::vector<VecLayer> layers,
    std::vector<std::uint8_t> decoded_bytes,
    std::size_t terminal_offset)
    : layers_(std::move(layers)),
      decoded_bytes_(std::move(decoded_bytes)),
      terminal_offset_(terminal_offset) {}

std::span<const std::uint8_t> VecResource::decoded_bytes() const noexcept {
    return decoded_bytes_;
}

VecResource decode_vec_resource(const std::filesystem::path& path) {
    return decode_vec_resource(read_file(path), resource_name_from_path(path));
}

VecResource decode_vec_resource(std::span<const std::uint8_t> bytes, std::string resource_name) {
    std::vector<std::uint8_t> current(bytes.begin(), bytes.end());
    std::vector<VecLayer> layers;

    while (true) {
        const auto header = parse_header(current, resource_name);
        VecLayer layer{
            current.size(),
            header.output_size,
            header.auxiliary,
            header.method_flags,
            header.checksum,
            header.method,
            header.final_layer,
            current.size() - header_size,
            VecMethod4{},
        };

        const auto payload = std::span<const std::uint8_t>(current).subspan(header_size);
        std::vector<std::uint8_t> decoded;
        switch (header.method) {
            case 4:
                decoded = decode_method_4(payload, resource_name, header.output_size, layer);
                break;
            case 12:
                decoded = decode_method_12(payload, resource_name, header.output_size, layer);
                break;
            default:
                fail_at(resource_name, 8, "VEC method has not been recovered");
        }
        if (decoded.size() != header.output_size) {
            fail_at(resource_name, current.size(), "VEC decoded output size mismatch");
        }

        layers.push_back(std::move(layer));
        if (header.final_layer) {
            return VecResource(std::move(layers), std::move(decoded), bytes.size());
        }
        current = std::move(decoded);
    }
}

}  // namespace bumpy

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace bumpy {

struct VecMethod4 {
    std::uint8_t marker{};
    std::size_t payload_size{};
};

struct VecMethod12 {
    std::uint16_t marker{};
    std::size_t mask_block_count{};
    std::size_t literal_count{};
    std::size_t payload_size{};
};

using VecCommand = std::variant<VecMethod4, VecMethod12>;

struct VecLayer {
    std::size_t input_size{};
    std::uint32_t output_size{};
    std::uint32_t auxiliary{};
    std::uint16_t method_flags{};
    std::uint16_t checksum{};
    std::uint8_t method{};
    bool final_layer{};
    std::size_t payload_size{};
    VecCommand command;
};

class VecResource {
public:
    VecResource(std::vector<VecLayer> layers, std::vector<std::uint8_t> decoded_bytes, std::size_t terminal_offset);

    [[nodiscard]] const std::vector<VecLayer>& layers() const noexcept { return layers_; }
    [[nodiscard]] std::span<const std::uint8_t> decoded_bytes() const noexcept;
    [[nodiscard]] std::size_t terminal_offset() const noexcept { return terminal_offset_; }

private:
    std::vector<VecLayer> layers_;
    std::vector<std::uint8_t> decoded_bytes_;
    std::size_t terminal_offset_{};
};

VecResource decode_vec_resource(const std::filesystem::path& path);
VecResource decode_vec_resource(std::span<const std::uint8_t> bytes, std::string resource_name);

}  // namespace bumpy

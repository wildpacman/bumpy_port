#include <catch2/catch_test_macros.hpp>

#include "resources/binary_reader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::filesystem::path write_fixture(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
    return path;
}

}  // namespace

TEST_CASE("binary reader reads little-endian integers and advances offsets") {
    const auto path = write_fixture("bumpy-binary-reader-integers.bin", {0x7f, 0x34, 0x12, 0xfe, 0xff});
    auto reader = bumpy::BinaryReader::from_file(path);

    REQUIRE(reader.size() == 5);
    REQUIRE(reader.offset() == 0);
    REQUIRE(reader.u8() == 0x7f);
    REQUIRE(reader.offset() == 1);
    REQUIRE(reader.u16_le() == 0x1234);
    REQUIRE(reader.offset() == 3);
    REQUIRE(reader.i16_le() == -2);
    REQUIRE(reader.offset() == 5);

    std::filesystem::remove(path);
}

TEST_CASE("binary reader seeks and returns byte spans without copying") {
    const auto path = write_fixture("bumpy-binary-reader-spans.bin", {0, 1, 2, 3, 4});
    auto reader = bumpy::BinaryReader::from_file(path);

    reader.seek(2);
    const auto bytes = reader.bytes(2);

    REQUIRE(bytes.size() == 2);
    REQUIRE(bytes[0] == 2);
    REQUIRE(bytes[1] == 3);
    REQUIRE(reader.offset() == 4);

    reader.seek(0);
    REQUIRE(reader.u8() == 0);

    std::filesystem::remove(path);
}

TEST_CASE("binary reader diagnostics include resource name and failing hex offset") {
    const auto path = write_fixture("tiny-resource.bin", {0xaa});
    auto reader = bumpy::BinaryReader::from_file(path);
    reader.seek(1);

    try {
        (void)reader.u16_le();
        FAIL("expected out_of_range");
    } catch (const std::out_of_range& error) {
        const std::string message = error.what();
        REQUIRE(message.find("tiny-resource.bin") != std::string::npos);
        REQUIRE(message.find("0x00000001") != std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST_CASE("binary reader rejects seeks outside the resource with hex offset diagnostics") {
    const auto path = write_fixture("tiny-seek-resource.bin", {0xaa});
    auto reader = bumpy::BinaryReader::from_file(path);

    try {
        reader.seek(2);
        FAIL("expected out_of_range");
    } catch (const std::out_of_range& error) {
        const std::string message = error.what();
        REQUIRE(message.find("tiny-seek-resource.bin") != std::string::npos);
        REQUIRE(message.find("0x00000002") != std::string::npos);
    }

    std::filesystem::remove(path);
}

#include "core/asset_manifest.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

void check(NTSTATUS status, const char* operation) {
    if (status < 0) {
        throw std::runtime_error(operation);
    }
}

std::string sha256_file(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    check(
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
        "BCryptOpenAlgorithmProvider failed");

    try {
        ULONG object_size{};
        ULONG copied{};
        check(
            BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&object_size),
                sizeof(object_size),
                &copied,
                0),
            "BCryptGetProperty failed");
        std::vector<UCHAR> hash_object(object_size);
        check(
            BCryptCreateHash(
                algorithm,
                &hash,
                hash_object.data(),
                static_cast<ULONG>(hash_object.size()),
                nullptr,
                0,
                0),
            "BCryptCreateHash failed");

        std::ifstream source(path, std::ios::binary);
        if (!source) {
            throw std::runtime_error("cannot open asset: " + path.string());
        }
        std::array<char, 64 * 1024> buffer{};
        while (source) {
            source.read(buffer.data(), buffer.size());
            const auto count = static_cast<ULONG>(source.gcount());
            if (count != 0) {
                check(
                    BCryptHashData(
                        hash,
                        reinterpret_cast<PUCHAR>(buffer.data()),
                        count,
                        0),
                    "BCryptHashData failed");
            }
        }
        if (!source.eof()) {
            throw std::runtime_error("cannot read asset: " + path.string());
        }

        std::array<UCHAR, 32> digest{};
        check(
            BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0),
            "BCryptFinishHash failed");
        BCryptDestroyHash(hash);
        hash = nullptr;
        BCryptCloseAlgorithmProvider(algorithm, 0);
        algorithm = nullptr;

        std::ostringstream text;
        text << std::hex << std::setfill('0');
        for (const auto byte : digest) {
            text << std::setw(2) << static_cast<unsigned>(byte);
        }
        return text.str();
    } catch (...) {
        if (hash) {
            BCryptDestroyHash(hash);
        }
        if (algorithm) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        throw;
    }
}

bool is_plain_filename(const std::string& name) {
    const std::filesystem::path path(name);
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos && !path.has_root_name() &&
           !path.has_root_directory();
}

}  // namespace

namespace bumpy {

AssetManifest AssetManifest::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open asset manifest: " + path.string());
    }

    AssetManifest result;
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() < 67 || line.substr(64, 2) != "  ") {
            throw std::runtime_error("invalid asset manifest line");
        }
        auto name = line.substr(66);
        if (!is_plain_filename(name)) {
            throw std::runtime_error("asset name is not a plain filename: " + name);
        }
        result.entries_.emplace_back(line.substr(0, 64), std::move(name));
    }
    return result;
}

AssetVerification AssetManifest::verify(const std::filesystem::path& root) const {
    AssetVerification result;
    result.file_count = entries_.size();
    for (const auto& [expected, name] : entries_) {
        const auto path = root / name;
        if (!std::filesystem::is_regular_file(path)) {
            result.missing.push_back(name);
        } else if (sha256_file(path) != expected) {
            result.changed.push_back(name);
        }
    }
    std::sort(result.missing.begin(), result.missing.end());
    std::sort(result.changed.begin(), result.changed.end());
    return result;
}

}  // namespace bumpy

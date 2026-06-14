#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace bumpy {

struct AssetVerification {
    std::vector<std::string> missing;
    std::vector<std::string> changed;
    std::size_t file_count{};
};

class AssetManifest {
public:
    static AssetManifest load(const std::filesystem::path& path);
    [[nodiscard]] AssetVerification verify(const std::filesystem::path& root) const;

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

}  // namespace bumpy

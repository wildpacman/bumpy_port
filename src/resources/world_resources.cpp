#include "resources/world_resources.h"

#include "resources/vec.h"

#include <sstream>

namespace bumpy {

WorldResources WorldResources::load(const std::filesystem::path& root, int world) {
    LevelResources level = LevelResources::load(root, world);  // D{world}.PAV/DEC/BUM

    std::ostringstream name;
    name << "MONDE" << world << ".VEC";
    const auto monde = decode_vec_resource(root / name.str());
    const auto bytes = monde.decoded_bytes();  // span into the local resource -> copy

    return WorldResources(world, std::move(level),
                          std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}

}  // namespace bumpy

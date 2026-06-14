#include <catch2/catch_test_macros.hpp>

#include "core/asset_manifest.h"

TEST_CASE("asset manifest recognizes the supplied BUMPY executable") {
    const auto manifest = bumpy::AssetManifest::load("config/original-assets.sha256");
    const auto result = manifest.verify(".");

    REQUIRE(result.missing.empty());
    REQUIRE(result.changed.empty());
    REQUIRE(result.file_count == 50);
}

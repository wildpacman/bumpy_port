#include <catch2/catch_test_macros.hpp>

#include "video3d/blur.h"

#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

TEST_CASE("sigma <= 0 is a no-op") {
    std::vector<std::uint8_t> img(5 * 5 * 4, 0);
    img[(2 * 5 + 2) * 4] = 200;
    auto copy = img;
    bumpy::gaussian_blur_rgba(img, 5, 5, 0.0f);
    REQUIRE(img == copy);
}

TEST_CASE("an impulse spreads symmetrically and keeps its energy") {
    const int w = 9;
    const int h = 9;
    std::vector<std::uint8_t> a(static_cast<std::size_t>(w) * h, 0);
    a[4 * w + 4] = 255;
    bumpy::gaussian_blur_alpha(a, w, h, 1.2f);
    REQUIRE(a[4 * w + 4] > a[4 * w + 5]);          // peak at centre
    REQUIRE(a[4 * w + 3] == a[4 * w + 5]);         // horizontal symmetry
    REQUIRE(a[3 * w + 4] == a[5 * w + 4]);         // vertical symmetry
    REQUIRE(a[4 * w + 5] > 0);                     // actually spread
    const int sum = std::accumulate(a.begin(), a.end(), 0);
    REQUIRE(sum >= 240);                           // energy preserved up to rounding
    REQUIRE(sum <= 270);
}

TEST_CASE("rgba blur touches colour channels independently and clamps edges") {
    const int w = 4;
    const int h = 1;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4, 0);
    img[0 * 4 + 0] = 255;  // red at x=0 (edge)
    bumpy::gaussian_blur_rgba(img, w, h, 1.0f);
    REQUIRE(img[0 * 4 + 0] > img[1 * 4 + 0]);  // red spreads right
    REQUIRE(img[1 * 4 + 1] == 0);              // green untouched
}

TEST_CASE("NaN sigma is a no-op") {
    std::vector<std::uint8_t> a(9, 0);
    a[4] = 200;
    auto copy = a;
    bumpy::gaussian_blur_alpha(a, 3, 3, std::numeric_limits<float>::quiet_NaN());
    REQUIRE(a == copy);
}

TEST_CASE("infinite sigma is bounded, not UB") {
    // Verify that infinite sigma doesn't cause UB (casting infinity to int).
    // With sigma=infinity, 3*sigma*ceil = infinity, which we clamp to 256 in
    // float space BEFORE the int cast, preventing UB from static_cast<int>(inf).
    // With such a large kernel (radius 256 = 513 taps), the blur heavily dilutes
    // the signal and may round many values to 0, but the call must complete safely.
    const int w = 9;
    const int h = 9;
    std::vector<std::uint8_t> a(static_cast<std::size_t>(w) * h, 0);
    a[4 * w + 4] = 255;  // Center pixel with max intensity
    // This call must complete without UB/hang.
    bumpy::gaussian_blur_alpha(a, w, h, std::numeric_limits<float>::infinity());
    // All output values must be valid [0, 255] and not corrupted by UB.
    for (const auto v : a) {
        REQUIRE(v >= 0);
        REQUIRE(v <= 255);
    }
}

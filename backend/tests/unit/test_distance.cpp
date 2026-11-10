#include <array>
#include <cmath>
#include <limits>
#include <span>

#include "engine/distance.h"
#include "framework/test_framework.h"

HNSWHY_TEST(distance_identical_is_zero) {
    const std::array<float, 3> a{1.0f, -2.0f, 3.5f};
    CHECK_NEAR(hnswhy::squared_l2(std::span<const float>(a), std::span<const float>(a)), 0.0, 0.0);
}

HNSWHY_TEST(distance_known_value) {
    const std::array<float, 2> a{0.0f, 0.0f};
    const std::array<float, 2> b{3.0f, 4.0f};
    CHECK_NEAR(hnswhy::squared_l2(std::span<const float>(a), std::span<const float>(b)), 25.0, 1e-6);
}

HNSWHY_TEST(distance_is_symmetric) {
    const std::array<float, 4> a{1.0f, 2.0f, 3.0f, 4.0f};
    const std::array<float, 4> b{-1.0f, 0.5f, 2.0f, 9.0f};
    const float ab = hnswhy::squared_l2(std::span<const float>(a), std::span<const float>(b));
    const float ba = hnswhy::squared_l2(std::span<const float>(b), std::span<const float>(a));
    CHECK_NEAR(ab, ba, 0.0);
}

HNSWHY_TEST(distance_dimension_mismatch_is_infinity) {
    const std::array<float, 2> a{1.0f, 2.0f};
    const std::array<float, 3> b{1.0f, 2.0f, 3.0f};
    CHECK(std::isinf(hnswhy::squared_l2(std::span<const float>(a), std::span<const float>(b))));
}

HNSWHY_TEST(distance_nan_is_infinity) {
    const std::array<float, 2> a{1.0f, std::numeric_limits<float>::quiet_NaN()};
    const std::array<float, 2> b{1.0f, 2.0f};
    CHECK(std::isinf(hnswhy::squared_l2(std::span<const float>(a), std::span<const float>(b))));
}

HNSWHY_TEST(distance_infinity_is_infinity) {
    const std::array<float, 2> a{1.0f, std::numeric_limits<float>::infinity()};
    const std::array<float, 2> b{1.0f, 2.0f};
    CHECK(std::isinf(hnswhy::squared_l2(std::span<const float>(a), std::span<const float>(b))));
}

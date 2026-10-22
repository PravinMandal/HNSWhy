#include "hnswhy/distance.h"

#include <gtest/gtest.h>

#include <array>

TEST(SquaredL2, ThreeFourFiveTriangle) {
    const std::array<float, 2> a{0.0F, 0.0F};
    const std::array<float, 2> b{3.0F, 4.0F};

    EXPECT_FLOAT_EQ(hnswhy::squared_l2(a.data(), b.data(), a.size()), 25.0F);
}

TEST(SquaredL2, NegativeCoordinates) {
    const std::array<float, 3> a{-1.0F, 0.0F, 2.0F};
    const std::array<float, 3> b{2.0F, 4.0F, -2.0F};

    EXPECT_FLOAT_EQ(hnswhy::squared_l2(a.data(), b.data(), a.size()), 41.0F);
}

TEST(SquaredL2, IdenticalVectorIsZero) {
    const std::array<float, 4> a{1.5F, -2.0F, 3.25F, 0.0F};

    EXPECT_FLOAT_EQ(hnswhy::squared_l2(a.data(), a.data(), a.size()), 0.0F);
}

TEST(SquaredL2, IsSymmetric) {
    const std::array<float, 2> a{1.0F, 2.0F};
    const std::array<float, 2> b{4.0F, 6.0F};

    EXPECT_FLOAT_EQ(hnswhy::squared_l2(a.data(), b.data(), a.size()),
                    hnswhy::squared_l2(b.data(), a.data(), b.size()));
}

TEST(SquaredL2, EmptyDimensionIsZero) {
    const std::array<float, 1> a{1.0F};
    const std::array<float, 1> b{2.0F};

    EXPECT_FLOAT_EQ(hnswhy::squared_l2(a.data(), b.data(), 0), 0.0F);
}

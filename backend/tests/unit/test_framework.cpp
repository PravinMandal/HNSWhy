#include <cmath>
#include <limits>

#include "framework/test_framework.h"

// The test framework is code too. A NaN result must fail CHECK_NEAR instead of
// slipping through: fabs(NaN) > eps is false, so the old comparison passed.

HNSWHY_TEST(check_near_accepts_values_inside_the_tolerance) {
    CHECK_NEAR(1.0, 1.05, 0.1);
    CHECK_NEAR(-2.5, -2.5, 0.0);
}

HNSWHY_TEST(check_near_rejects_nan) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    bool failed = false;
    try {
        CHECK_NEAR(nan, 1.0, 0.0);
    } catch (const hnswhytest::Failure&) {
        failed = true;
    }
    CHECK(failed);
}

HNSWHY_TEST(check_near_rejects_infinity) {
    const double infinity = std::numeric_limits<double>::infinity();
    bool failed = false;
    try {
        CHECK_NEAR(infinity, 1.0, 0.0);
    } catch (const hnswhytest::Failure&) {
        failed = true;
    }
    CHECK(failed);
}

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

#include "bench/cli.h"
#include "framework/test_framework.h"

// The benchmark CLI used std::stoull directly, which accepts "-1" and wraps it
// to a huge size_t. A negative --passes would then look like a near-infinite
// run instead of a usage error.

HNSWHY_TEST(bench_parse_size_accepts_decimal_digits) {
    CHECK_EQ(hnswhy::bench::parse_size("0", "--x"), std::size_t{0});
    CHECK_EQ(hnswhy::bench::parse_size("42", "--x"), std::size_t{42});
    CHECK_EQ(hnswhy::bench::parse_size("0010", "--x"), std::size_t{10});
}

HNSWHY_TEST(bench_parse_size_rejects_negative_and_signed_values) {
    CHECK_THROWS_AS(hnswhy::bench::parse_size("-1", "--x"), std::invalid_argument);
    CHECK_THROWS_AS(hnswhy::bench::parse_size("+1", "--x"), std::invalid_argument);
}

HNSWHY_TEST(bench_parse_size_rejects_empty_and_junk) {
    CHECK_THROWS_AS(hnswhy::bench::parse_size("", "--x"), std::invalid_argument);
    CHECK_THROWS_AS(hnswhy::bench::parse_size("1x", "--x"), std::invalid_argument);
    CHECK_THROWS_AS(hnswhy::bench::parse_size(" 1", "--x"), std::invalid_argument);
    CHECK_THROWS_AS(hnswhy::bench::parse_size("1 ", "--x"), std::invalid_argument);
}

HNSWHY_TEST(bench_parse_size_rejects_overflow) {
    CHECK_THROWS_AS(hnswhy::bench::parse_size("999999999999999999999999999999", "--x"),
                    std::out_of_range);
}

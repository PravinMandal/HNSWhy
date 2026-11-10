#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "engine/metrics.h"
#include "framework/test_framework.h"

HNSWHY_TEST(metrics_recall_is_one_for_exact_results) {
    const std::vector<float> truth{1.0f, 2.0f, 3.0f};
    const std::vector<float> result{1.0f, 2.0f, 3.0f};
    CHECK_NEAR(hnswhy::tie_aware_recall_at_k(result, truth, 3), 1.0, 0.0);
}

HNSWHY_TEST(metrics_recall_counts_partial_hits) {
    const std::vector<float> truth{1.0f, 2.0f, 3.0f};
    const std::vector<float> result{1.0f, 4.0f, 5.0f};
    CHECK_NEAR(hnswhy::tie_aware_recall_at_k(result, truth, 3), 1.0 / 3.0, 1e-12);
}

HNSWHY_TEST(metrics_recall_counts_ties_at_the_cutoff) {
    const std::vector<float> truth{1.0f, 2.0f, 3.0f};
    const std::vector<float> result{1.0f, 2.0f, 3.0f + 1e-6f};
    CHECK_NEAR(hnswhy::tie_aware_recall_at_k(result, truth, 3), 1.0, 0.0);
}

HNSWHY_TEST(metrics_recall_handles_short_results_and_zero_k) {
    const std::vector<float> truth{1.0f, 2.0f, 3.0f};
    const std::vector<float> result{1.0f};
    CHECK_NEAR(hnswhy::tie_aware_recall_at_k(result, truth, 3), 1.0 / 3.0, 1e-12);
    CHECK_NEAR(hnswhy::tie_aware_recall_at_k(result, truth, 0), 1.0, 0.0);
}

HNSWHY_TEST(metrics_percentile_uses_nearest_rank) {
    const std::vector<double> samples{5.0, 1.0, 3.0, 2.0, 4.0};
    CHECK_NEAR(hnswhy::percentile(samples, 0.0), 1.0, 0.0);
    CHECK_NEAR(hnswhy::percentile(samples, 50.0), 3.0, 0.0);
    CHECK_NEAR(hnswhy::percentile(samples, 95.0), 5.0, 0.0);
    CHECK_NEAR(hnswhy::percentile(samples, 100.0), 5.0, 0.0);
    CHECK_NEAR(hnswhy::percentile({}, 50.0), 0.0, 0.0);
    CHECK_THROWS_AS(hnswhy::percentile(samples, 101.0), std::invalid_argument);
    CHECK_THROWS_AS(hnswhy::percentile(samples, std::nan("")), std::invalid_argument);
    CHECK_THROWS_AS(hnswhy::percentile({1.0, std::nan("")}, 50.0), std::invalid_argument);
    CHECK_THROWS_AS(hnswhy::percentile({1.0, std::numeric_limits<double>::infinity()}, 50.0),
                    std::invalid_argument);
}

HNSWHY_TEST(metrics_latency_summary) {
    const hnswhy::LatencySummary summary =
        hnswhy::summarize_latency({10.0, 20.0, 30.0, 40.0, 50.0});
    CHECK_NEAR(summary.mean, 30.0, 0.0);
    CHECK_NEAR(summary.p50, 30.0, 0.0);
    CHECK_NEAR(summary.p95, 50.0, 0.0);
    CHECK_NEAR(summary.p99, 50.0, 0.0);
    CHECK_NEAR(hnswhy::summarize_latency({}).mean, 0.0, 0.0);
    CHECK_THROWS_AS(hnswhy::summarize_latency({1.0, std::nan("")}), std::invalid_argument);
    CHECK_THROWS_AS(hnswhy::summarize_latency({1.0, -1.0}), std::invalid_argument);
}

HNSWHY_TEST(metrics_qps) {
    CHECK_NEAR(hnswhy::queries_per_second(100, 2.0), 50.0, 1e-12);
    CHECK_NEAR(hnswhy::queries_per_second(100, 0.0), 0.0, 0.0);
}

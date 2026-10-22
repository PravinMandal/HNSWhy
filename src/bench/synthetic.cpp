#include "synthetic.h"

#include "hnswhy/distance.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

namespace hnswhy::bench {
namespace {

struct ScanResult {
    std::size_t best_id = 0;
    float best_distance = std::numeric_limits<float>::infinity();
};

ScanResult exact_scan(const float* query, const std::vector<float>& base,
                      std::size_t vectors, std::size_t dim) {
    ScanResult result;

    for (std::size_t i = 0; i < vectors; ++i) {
        const float distance =
            squared_l2(query, base.data() + (i * dim), dim);
        if (distance < result.best_distance) {
            result.best_distance = distance;
            result.best_id = i;
        }
    }

    return result;
}

double percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) {
        return 0.0;
    }
    if (sorted.size() == 1) {
        return sorted.front();
    }

    const double position = fraction * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(position);
    const auto upper = std::min(lower + 1, sorted.size() - 1);
    const double weight = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 0) {
        return (values[middle - 1] + values[middle]) / 2.0;
    }
    return values[middle];
}

struct TrialResult {
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    std::uint64_t checksum = 0;
};

TrialResult run_trial(const std::vector<float>& base,
                      const std::vector<float>& queries, std::size_t vectors,
                      std::size_t dim) {
    std::vector<double> latencies;
    latencies.reserve(queries.size() / dim);
    std::uint64_t checksum = 0;

    for (std::size_t query_id = 0; query_id < queries.size() / dim; ++query_id) {
        const float* query = queries.data() + (query_id * dim);

        const auto start = std::chrono::steady_clock::now();
        const ScanResult result = exact_scan(query, base, vectors, dim);
        const auto end = std::chrono::steady_clock::now();

        const std::chrono::duration<double, std::milli> elapsed = end - start;
        latencies.push_back(elapsed.count());
        checksum += result.best_id;
    }

    std::sort(latencies.begin(), latencies.end());
    return TrialResult{
        percentile(latencies, 0.50),
        percentile(latencies, 0.95),
        checksum,
    };
}

}  // namespace

void run_synthetic(const SyntheticConfig& config) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> values(-1.0F, 1.0F);

    std::vector<float> base(config.vectors * config.dim);
    std::vector<float> queries(config.queries * config.dim);
    for (float& value : base) {
        value = values(rng);
    }
    for (float& value : queries) {
        value = values(rng);
    }

    std::cout << "Synthetic exact scan (k=1)\n"
              << "  vectors: " << config.vectors << "  dim: " << config.dim
              << "  queries: " << config.queries
              << "  warmup: " << config.warmup
              << "  trials: " << config.trials << '\n';

    std::vector<double> trial_p50;
    std::vector<double> trial_p95;
    std::uint64_t checksum = 0;

    for (std::size_t trial = 0; trial < config.trials; ++trial) {
        for (std::size_t warmup = 0; warmup < config.warmup; ++warmup) {
            const std::size_t query_id = warmup % config.queries;
            const float* query = queries.data() + (query_id * config.dim);
            (void)exact_scan(query, base, config.vectors, config.dim);
        }

        const TrialResult result =
            run_trial(base, queries, config.vectors, config.dim);
        trial_p50.push_back(result.p50_ms);
        trial_p95.push_back(result.p95_ms);
        checksum += result.checksum;

        std::cout << "  trial " << trial << ": p50 " << std::fixed
                  << std::setprecision(3) << result.p50_ms << " ms  p95 "
                  << result.p95_ms << " ms\n";
    }

    std::cout << "  median: p50 " << std::fixed << std::setprecision(3)
              << median(trial_p50) << " ms  p95 " << median(trial_p95)
              << " ms\n"
              << "  checksum: " << checksum << '\n';
}

}  // namespace hnswhy::bench

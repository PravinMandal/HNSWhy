#include "engine/metrics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hnswhy {

double tie_aware_recall_at_k(std::span<const float> result_distances,
                             std::span<const float> truth_distances, std::size_t k) {
    const std::size_t k_eff = std::min(k, truth_distances.size());
    if (k_eff == 0) {
        return 1.0;
    }

    const float cutoff = truth_distances[k_eff - 1];
    const float epsilon = 1e-5f * std::max(1.0f, std::fabs(cutoff));
    std::size_t hits = 0;
    const std::size_t considered = std::min(k_eff, result_distances.size());
    for (std::size_t i = 0; i < considered; ++i) {
        if (result_distances[i] <= cutoff + epsilon) {
            ++hits;
        }
    }
    return static_cast<double>(hits) / static_cast<double>(k_eff);
}

double percentile(std::vector<double> samples, double p) {
    if (samples.empty()) {
        return 0.0;
    }
    if (!std::isfinite(p) || p < 0.0 || p > 100.0) {
        throw std::invalid_argument("percentile: p must be finite and in [0, 100]");
    }
    for (const double sample : samples) {
        if (!std::isfinite(sample)) {
            throw std::invalid_argument("percentile: samples must be finite");
        }
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t rank =
        static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(samples.size())));
    const std::size_t index = std::min(samples.size() - 1, rank == 0 ? 0 : rank - 1);
    return samples[index];
}

LatencySummary summarize_latency(std::vector<double> samples_ms) {
    if (samples_ms.empty()) {
        return {};
    }

    LatencySummary summary;
    double total = 0.0;
    for (const double sample : samples_ms) {
        if (!std::isfinite(sample) || sample < 0.0) {
            throw std::invalid_argument("summarize_latency: samples must be finite and non-negative");
        }
        total += sample;
    }
    summary.mean = total / static_cast<double>(samples_ms.size());

    std::sort(samples_ms.begin(), samples_ms.end());
    auto percentile_of_sorted = [&samples_ms](double p) {
        const std::size_t rank = static_cast<std::size_t>(
            std::ceil(p / 100.0 * static_cast<double>(samples_ms.size())));
        const std::size_t index =
            std::min(samples_ms.size() - 1, rank == 0 ? 0 : rank - 1);
        return samples_ms[index];
    };
    summary.p50 = percentile_of_sorted(50.0);
    summary.p95 = percentile_of_sorted(95.0);
    summary.p99 = percentile_of_sorted(99.0);
    return summary;
}

double queries_per_second(std::size_t query_count, double elapsed_seconds) {
    if (elapsed_seconds <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(query_count) / elapsed_seconds;
}

}  // namespace hnswhy

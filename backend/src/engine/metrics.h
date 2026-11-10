#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace vf {

// Recall@k with distance ties counted as hits. The k-th truth distance is the
// cutoff; a returned distance within eps of it is as good as a correct ID,
// because equal-distance neighbours are interchangeable.
double tie_aware_recall_at_k(std::span<const float> result_distances,
                             std::span<const float> truth_distances, std::size_t k);

// Nearest-rank percentile. p is in [0, 100]. The input is not modified.
double percentile(std::vector<double> samples, double p);

struct LatencySummary {
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

LatencySummary summarize_latency(std::vector<double> samples_ms);

double queries_per_second(std::size_t query_count, double elapsed_seconds);

}  // namespace vf

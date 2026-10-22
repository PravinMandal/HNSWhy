#pragma once

#include <cstddef>

namespace hnswhy::bench {

struct SyntheticConfig {
    std::size_t vectors = 20'000;
    std::size_t dim = 128;
    std::size_t queries = 100;
    std::size_t warmup = 10;
    std::size_t trials = 3;
};

// Deterministic exact-scan timing on synthetic vectors. This is a smoke test for
// the distance kernel and the timer, not a benchmark result.
void run_synthetic(const SyntheticConfig& config);

}  // namespace hnswhy::bench

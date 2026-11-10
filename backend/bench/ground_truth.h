#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "bench/fvecs.h"
#include "engine/vector_store.h"

namespace vf {

// Exact top-k answer key: IDs and distances, row-major, n * k.
struct GroundTruth {
    std::size_t n = 0;
    std::size_t k = 0;
    std::vector<std::int32_t> ids;
    std::vector<float> distances;
};

using ProgressFn = std::function<void(std::size_t done, std::size_t total)>;

// Brute-force top-k for every query. k must be in [1, base.size()].
GroundTruth compute_ground_truth(const VectorStore& base, const FloatMatrix& queries,
                                 std::size_t k, const ProgressFn& progress = {});

}  // namespace vf

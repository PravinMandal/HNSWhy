#include "bench/ground_truth.h"

#include <stdexcept>

#include "engine/brute_force.h"

namespace vf {

GroundTruth compute_ground_truth(const VectorStore& base, const FloatMatrix& queries,
                                 std::size_t k, const ProgressFn& progress) {
    if (k == 0 || k > base.size()) {
        throw std::invalid_argument("compute_ground_truth: k must be in [1, base.size()]");
    }
    if (queries.dim != base.dim()) {
        throw std::invalid_argument("compute_ground_truth: query dimension mismatch");
    }

    GroundTruth truth;
    truth.n = queries.n;
    truth.k = k;
    truth.ids.resize(queries.n * k);
    truth.distances.resize(queries.n * k);

    const BruteForce brute_force(base);
    for (std::size_t q = 0; q < queries.n; ++q) {
        const SearchResult result = brute_force.search(queries.row(q), k);
        for (std::size_t j = 0; j < k; ++j) {
            truth.ids[q * k + j] = static_cast<std::int32_t>(result.ids[j]);
            truth.distances[q * k + j] = result.distances[j];
        }
        if (progress) {
            progress(q + 1, queries.n);
        }
    }
    return truth;
}

}  // namespace vf

#pragma once

#include <cstddef>
#include <span>

#include "engine/types.h"
#include "engine/vector_store.h"

namespace vf {

// Exact k-nearest-neighbour search. This is the answer key for tests and the
// benchmark baseline; it is never the product search path.
class BruteForce {
public:
    explicit BruteForce(const VectorStore& store) : store_(store) {}

    // Returns up to k results sorted by (distance, slot). k == 0 or an empty
    // store returns an empty result.
    SearchResult search(std::span<const float> query, std::size_t k) const;

private:
    const VectorStore& store_;
};

}  // namespace vf

#pragma once

#include <cstddef>

namespace hnswhy {

// Squared Euclidean distance between two vectors of `dim` floats.
// Preconditions: `a` and `b` point to at least `dim` floats. `dim == 0` returns 0.
// NaN/Inf are outside the v1 data contract; validate at the dataset boundary.
[[nodiscard]] float squared_l2(const float* a, const float* b, std::size_t dim) noexcept;

}  // namespace hnswhy

#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace hnswhy {

// Squared Euclidean distance. Non-finite inputs (NaN/Inf) and mismatched
// dimensions produce +inf so callers treat them as "not a candidate".
inline float squared_l2(std::span<const float> a, std::span<const float> b) noexcept {
    if (a.size() != b.size()) {
        return std::numeric_limits<float>::infinity();
    }

    float sum = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::isfinite(sum) ? sum : std::numeric_limits<float>::infinity();
}

}  // namespace hnswhy

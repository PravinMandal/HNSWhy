#include "hnswhy/distance.h"

namespace hnswhy {

float squared_l2(const float* a, const float* b, std::size_t dim) noexcept {
    float sum = 0.0F;

    for (std::size_t i = 0; i < dim; ++i) {
        const float diff = a[i] - b[i];
        sum += diff * diff;
    }

    return sum;
}

}  // namespace hnswhy

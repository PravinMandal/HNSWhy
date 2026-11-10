#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "engine/types.h"

namespace vf {

// Flat, owning vector storage. Slot IDs are indices into one contiguous float
// buffer and never change for the lifetime of the store.
class VectorStore {
public:
    void reserve(std::size_t n, std::size_t dim);

    // Appends one vector. The first add fixes the dimension if reserve() was
    // not called. Throws std::invalid_argument on a dimension mismatch.
    Slot add(std::span<const float> v);

    std::span<const float> get(Slot id) const;

    // Bulk-load support: appends n uninitialized vectors and returns the
    // n * dim floats to fill. The store is not safe to search until every row
    // has been written. Throws std::logic_error if dim is unknown.
    std::span<float> append_uninitialized(std::size_t n);

    std::size_t size() const noexcept { return size_; }
    std::size_t dim() const noexcept { return dim_; }

private:
    std::vector<float> data_;
    std::size_t dim_ = 0;
    std::size_t size_ = 0;
};

}  // namespace vf

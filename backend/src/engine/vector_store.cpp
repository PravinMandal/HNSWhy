#include "engine/vector_store.h"

#include <limits>
#include <stdexcept>

namespace vf {

void VectorStore::reserve(std::size_t n, std::size_t dim) {
    if (dim == 0) {
        throw std::invalid_argument("VectorStore::reserve: dim must be positive");
    }
    if (dim_ != 0 && dim_ != dim) {
        throw std::invalid_argument("VectorStore::reserve: dimension mismatch");
    }
    if (size_ != 0) {
        throw std::logic_error("VectorStore::reserve: store is not empty");
    }
    dim_ = dim;
    data_.reserve(n * dim);
}

Slot VectorStore::add(std::span<const float> v) {
    if (v.empty()) {
        throw std::invalid_argument("VectorStore::add: empty vector");
    }
    if (dim_ == 0) {
        dim_ = v.size();
    } else if (v.size() != dim_) {
        throw std::invalid_argument("VectorStore::add: dimension mismatch");
    }
    if (size_ == static_cast<std::size_t>(kInvalidSlot)) {
        throw std::overflow_error("VectorStore::add: slot space exhausted");
    }

    const Slot id = static_cast<Slot>(size_);
    data_.insert(data_.end(), v.begin(), v.end());
    ++size_;
    return id;
}

std::span<const float> VectorStore::get(Slot id) const {
    if (id >= size_) {
        throw std::out_of_range("VectorStore::get: slot out of range");
    }
    return std::span<const float>(data_.data() + static_cast<std::size_t>(id) * dim_, dim_);
}

std::span<float> VectorStore::append_uninitialized(std::size_t n) {
    if (n == 0) {
        return {};
    }
    if (dim_ == 0) {
        throw std::logic_error("VectorStore::append_uninitialized: dimension unknown");
    }
    if (n > static_cast<std::size_t>(kInvalidSlot) - size_) {
        throw std::overflow_error("VectorStore::append_uninitialized: slot space exhausted");
    }

    const std::size_t old_size = size_;
    data_.resize((old_size + n) * dim_);
    size_ = old_size + n;
    return std::span<float>(data_.data() + old_size * dim_, n * dim_);
}

}  // namespace vf

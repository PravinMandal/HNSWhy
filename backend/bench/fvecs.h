#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "engine/vector_store.h"

namespace hnswhy {

// In-memory float matrix for query sets and other small fvecs files.
struct FloatMatrix {
    std::size_t n = 0;
    std::size_t dim = 0;
    std::vector<float> data;

    std::span<const float> row(std::size_t i) const {
        return std::span<const float>(data.data() + i * dim, dim);
    }
};

// Ground-truth IDs or any other ivecs file.
struct IntMatrix {
    std::size_t n = 0;
    std::size_t dim = 0;
    std::vector<std::int32_t> data;

    std::span<const std::int32_t> row(std::size_t i) const {
        return std::span<const std::int32_t>(data.data() + i * dim, dim);
    }
};

// All loaders throw std::runtime_error on missing/truncated/malformed files.
FloatMatrix load_fvecs(const std::filesystem::path& path);
IntMatrix load_ivecs(const std::filesystem::path& path);

// Loads a base set directly into a VectorStore without an intermediate copy.
// max_n == 0 loads the whole file; otherwise the first max_n records are used.
VectorStore load_fvecs_store(const std::filesystem::path& path, std::size_t max_n = 0);

void write_fvecs(const std::filesystem::path& path, std::span<const float> data,
                 std::size_t n, std::size_t dim);
void write_ivecs(const std::filesystem::path& path, std::span<const std::int32_t> data,
                 std::size_t n, std::size_t dim);

}  // namespace hnswhy

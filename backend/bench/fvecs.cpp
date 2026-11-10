#include "bench/fvecs.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace vf {
namespace {

static_assert(std::endian::native == std::endian::little,
              "fvecs/ivecs loaders assume a little-endian host");

constexpr std::uint32_t kMinDim = 1;
constexpr std::uint32_t kMaxDim = 4096;

std::ifstream open_binary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return in;
}

std::uint64_t file_size(std::ifstream& in, const std::filesystem::path& path) {
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        throw std::runtime_error("cannot size " + path.string());
    }
    in.seekg(0, std::ios::beg);
    return static_cast<std::uint64_t>(size);
}

struct Layout {
    std::uint32_t dim = 0;
    std::uint64_t records = 0;
    std::uint64_t record_size = 0;
};

Layout inspect(std::ifstream& in, const std::filesystem::path& path) {
    std::uint32_t dim = 0;
    in.read(reinterpret_cast<char*>(&dim), sizeof(dim));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(dim))) {
        throw std::runtime_error(path.string() + ": missing dimension header");
    }
    if (dim < kMinDim || dim > kMaxDim) {
        throw std::runtime_error(path.string() + ": implausible dim " + std::to_string(dim));
    }

    const std::uint64_t size = file_size(in, path);
    const std::uint64_t record_size = 4ULL + static_cast<std::uint64_t>(dim) * 4ULL;
    if (size < record_size || size % record_size != 0) {
        throw std::runtime_error(path.string() + ": size is not a whole number of records");
    }

    in.seekg(0, std::ios::beg);
    return Layout{dim, size / record_size, record_size};
}

void read_records(std::ifstream& in, const std::filesystem::path& path, float* dst,
                  std::uint64_t records, std::uint32_t dim) {
    for (std::uint64_t i = 0; i < records; ++i) {
        std::uint32_t record_dim = 0;
        in.read(reinterpret_cast<char*>(&record_dim), sizeof(record_dim));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(record_dim)) || record_dim != dim) {
            throw std::runtime_error(path.string() + ": inconsistent record dimension");
        }
        in.read(reinterpret_cast<char*>(dst + i * dim),
                static_cast<std::streamsize>(sizeof(float) * dim));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(float) * dim)) {
            throw std::runtime_error(path.string() + ": truncated record");
        }
    }
}

void read_records(std::ifstream& in, const std::filesystem::path& path, std::int32_t* dst,
                  std::uint64_t records, std::uint32_t dim) {
    for (std::uint64_t i = 0; i < records; ++i) {
        std::uint32_t record_dim = 0;
        in.read(reinterpret_cast<char*>(&record_dim), sizeof(record_dim));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(record_dim)) || record_dim != dim) {
            throw std::runtime_error(path.string() + ": inconsistent record dimension");
        }
        in.read(reinterpret_cast<char*>(dst + i * dim),
                static_cast<std::streamsize>(sizeof(std::int32_t) * dim));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(std::int32_t) * dim)) {
            throw std::runtime_error(path.string() + ": truncated record");
        }
    }
}

template <typename T>
void write_matrix(const std::filesystem::path& path, std::span<const T> data, std::size_t n,
                  std::size_t dim) {
    if (dim == 0 || dim > kMaxDim) {
        throw std::invalid_argument("write_matrix: invalid dim");
    }
    if (data.size() != n * dim) {
        throw std::invalid_argument("write_matrix: data size does not match n * dim");
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot open " + path.string() + " for writing");
    }

    const std::uint32_t header = static_cast<std::uint32_t>(dim);
    for (std::size_t i = 0; i < n; ++i) {
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(data.data() + i * dim),
                  static_cast<std::streamsize>(sizeof(T) * dim));
    }
    if (!out) {
        throw std::runtime_error(path.string() + ": write failed");
    }
}

}  // namespace

FloatMatrix load_fvecs(const std::filesystem::path& path) {
    auto in = open_binary(path);
    const Layout layout = inspect(in, path);

    FloatMatrix matrix;
    matrix.n = static_cast<std::size_t>(layout.records);
    matrix.dim = layout.dim;
    matrix.data.resize(matrix.n * matrix.dim);
    read_records(in, path, matrix.data.data(), layout.records, layout.dim);
    return matrix;
}

IntMatrix load_ivecs(const std::filesystem::path& path) {
    auto in = open_binary(path);
    const Layout layout = inspect(in, path);

    IntMatrix matrix;
    matrix.n = static_cast<std::size_t>(layout.records);
    matrix.dim = layout.dim;
    matrix.data.resize(matrix.n * matrix.dim);
    read_records(in, path, matrix.data.data(), layout.records, layout.dim);
    return matrix;
}

VectorStore load_fvecs_store(const std::filesystem::path& path, std::size_t max_n) {
    auto in = open_binary(path);
    const Layout layout = inspect(in, path);

    const std::size_t records = max_n == 0
                                    ? static_cast<std::size_t>(layout.records)
                                    : std::min<std::size_t>(max_n, layout.records);
    if (records == 0) {
        throw std::runtime_error(path.string() + ": no records to load");
    }

    VectorStore store;
    store.reserve(records, layout.dim);
    std::span<float> dst = store.append_uninitialized(records);
    read_records(in, path, dst.data(), records, layout.dim);
    return store;
}

void write_fvecs(const std::filesystem::path& path, std::span<const float> data, std::size_t n,
                 std::size_t dim) {
    write_matrix(path, data, n, dim);
}

void write_ivecs(const std::filesystem::path& path, std::span<const std::int32_t> data,
                 std::size_t n, std::size_t dim) {
    write_matrix(path, data, n, dim);
}

}  // namespace vf

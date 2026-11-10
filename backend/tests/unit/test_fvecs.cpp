#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bench/fvecs.h"
#include "framework/test_framework.h"

namespace {

struct TempFile {
    std::filesystem::path path;

    explicit TempFile(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("hnswhy_test_" + name)) {
        std::filesystem::remove(path);
    }

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

}  // namespace

HNSWHY_TEST(fvecs_roundtrip_and_store_load) {
    const std::vector<float> data{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    TempFile file("roundtrip.fvecs");
    hnswhy::write_fvecs(file.path, data, 2, 3);

    const hnswhy::FloatMatrix matrix = hnswhy::load_fvecs(file.path);
    CHECK_EQ(matrix.n, std::size_t{2});
    CHECK_EQ(matrix.dim, std::size_t{3});
    CHECK_NEAR(matrix.data[4], 5.0, 0.0);

    const hnswhy::VectorStore store = hnswhy::load_fvecs_store(file.path);
    CHECK_EQ(store.size(), std::size_t{2});
    CHECK_EQ(store.dim(), std::size_t{3});
    CHECK_NEAR(store.get(1)[2], 6.0, 0.0);

    const hnswhy::VectorStore prefix = hnswhy::load_fvecs_store(file.path, 1);
    CHECK_EQ(prefix.size(), std::size_t{1});
}

HNSWHY_TEST(ivecs_roundtrip) {
    const std::vector<std::int32_t> data{1, 2, 3, 4};
    TempFile file("roundtrip.ivecs");
    hnswhy::write_ivecs(file.path, data, 2, 2);

    const hnswhy::IntMatrix matrix = hnswhy::load_ivecs(file.path);
    CHECK_EQ(matrix.n, std::size_t{2});
    CHECK_EQ(matrix.dim, std::size_t{2});
    CHECK_EQ(matrix.data[3], std::int32_t{4});
}

HNSWHY_TEST(fvecs_rejects_truncated_file) {
    const std::vector<float> data{1.0f, 2.0f, 3.0f, 4.0f};
    TempFile file("truncated.fvecs");
    hnswhy::write_fvecs(file.path, data, 2, 2);
    std::filesystem::resize_file(file.path, std::filesystem::file_size(file.path) - 1);

    CHECK_THROWS_AS(hnswhy::load_fvecs(file.path), std::runtime_error);
}

HNSWHY_TEST(fvecs_rejects_implausible_dimension) {
    TempFile file("bad_dim.fvecs");
    {
        std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
        const std::int32_t dim = 0;
        out.write(reinterpret_cast<const char*>(&dim), sizeof(dim));
    }
    CHECK_THROWS_AS(hnswhy::load_fvecs(file.path), std::runtime_error);
}

HNSWHY_TEST(fvecs_rejects_missing_file) {
    CHECK_THROWS_AS(hnswhy::load_fvecs("/nonexistent/hnswhy_test_missing.fvecs"), std::runtime_error);
}

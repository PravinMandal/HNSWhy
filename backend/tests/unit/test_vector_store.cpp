#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>

#include "engine/vector_store.h"
#include "framework/test_framework.h"

VF_TEST(vector_store_add_and_get) {
    vf::VectorStore store;
    const std::array<float, 3> a{1.0f, 2.0f, 3.0f};
    const std::array<float, 3> b{4.0f, 5.0f, 6.0f};

    CHECK_EQ(store.add(std::span<const float>(a)), vf::Slot{0});
    CHECK_EQ(store.add(std::span<const float>(b)), vf::Slot{1});
    CHECK_EQ(store.size(), std::size_t{2});
    CHECK_EQ(store.dim(), std::size_t{3});
    CHECK_NEAR(store.get(0)[0], 1.0, 0.0);
    CHECK_NEAR(store.get(1)[2], 6.0, 0.0);
}

VF_TEST(vector_store_reserved_append) {
    vf::VectorStore store;
    store.reserve(2, 2);

    std::span<float> rows = store.append_uninitialized(2);
    CHECK_EQ(rows.size(), std::size_t{4});
    rows[0] = 1.0f;
    rows[1] = 2.0f;
    rows[2] = 3.0f;
    rows[3] = 4.0f;

    CHECK_EQ(store.size(), std::size_t{2});
    CHECK_EQ(store.dim(), std::size_t{2});
    CHECK_NEAR(store.get(0)[1], 2.0, 0.0);
    CHECK_NEAR(store.get(1)[0], 3.0, 0.0);
}

VF_TEST(vector_store_rejects_dimension_mismatch) {
    vf::VectorStore store;
    const std::array<float, 3> a{1.0f, 2.0f, 3.0f};
    const std::array<float, 2> b{1.0f, 2.0f};
    store.add(std::span<const float>(a));
    CHECK_THROWS_AS(store.add(std::span<const float>(b)), std::invalid_argument);
}

VF_TEST(vector_store_rejects_out_of_range_slot) {
    vf::VectorStore store;
    const std::array<float, 2> a{1.0f, 2.0f};
    store.add(std::span<const float>(a));
    CHECK_THROWS_AS(store.get(1), std::out_of_range);
}

VF_TEST(vector_store_rejects_empty_vector) {
    vf::VectorStore store;
    CHECK_THROWS_AS(store.add(std::span<const float>()), std::invalid_argument);
}

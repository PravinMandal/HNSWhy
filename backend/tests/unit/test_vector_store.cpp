#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>

#include "engine/vector_store.h"
#include "framework/test_framework.h"

HNSWHY_TEST(vector_store_add_and_get) {
    hnswhy::VectorStore store;
    const std::array<float, 3> a{1.0f, 2.0f, 3.0f};
    const std::array<float, 3> b{4.0f, 5.0f, 6.0f};

    CHECK_EQ(store.add(std::span<const float>(a)), hnswhy::Slot{0});
    CHECK_EQ(store.add(std::span<const float>(b)), hnswhy::Slot{1});
    CHECK_EQ(store.size(), std::size_t{2});
    CHECK_EQ(store.dim(), std::size_t{3});
    CHECK_NEAR(store.get(0)[0], 1.0, 0.0);
    CHECK_NEAR(store.get(1)[2], 6.0, 0.0);
}

HNSWHY_TEST(vector_store_reserved_append) {
    hnswhy::VectorStore store;
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

HNSWHY_TEST(vector_store_rejects_dimension_mismatch) {
    hnswhy::VectorStore store;
    const std::array<float, 3> a{1.0f, 2.0f, 3.0f};
    const std::array<float, 2> b{1.0f, 2.0f};
    store.add(std::span<const float>(a));
    CHECK_THROWS_AS(store.add(std::span<const float>(b)), std::invalid_argument);
}

HNSWHY_TEST(vector_store_rejects_out_of_range_slot) {
    hnswhy::VectorStore store;
    const std::array<float, 2> a{1.0f, 2.0f};
    store.add(std::span<const float>(a));
    CHECK_THROWS_AS(store.get(1), std::out_of_range);
}

HNSWHY_TEST(vector_store_rejects_empty_vector) {
    hnswhy::VectorStore store;
    CHECK_THROWS_AS(store.add(std::span<const float>()), std::invalid_argument);
}

HNSWHY_TEST(vector_store_move_resets_the_source) {
    hnswhy::VectorStore source;
    const std::array<float, 2> a{1.0f, 2.0f};
    source.add(std::span<const float>(a));

    hnswhy::VectorStore moved(std::move(source));
    CHECK_EQ(moved.size(), std::size_t{1});
    CHECK_EQ(moved.dim(), std::size_t{2});
    CHECK_EQ(source.size(), std::size_t{0});
    CHECK_EQ(source.dim(), std::size_t{0});
    CHECK_THROWS_AS(source.get(0), std::out_of_range);
}

HNSWHY_TEST(vector_store_add_accepts_a_view_of_its_own_buffer) {
    hnswhy::VectorStore store;
    const std::array<float, 2> a{1.0f, 2.0f};
    store.add(std::span<const float>(a));
    const hnswhy::Slot id = store.add(store.get(0));
    CHECK_EQ(id, hnswhy::Slot{1});
    CHECK_NEAR(store.get(1)[0], 1.0, 0.0);
    CHECK_NEAR(store.get(1)[1], 2.0, 0.0);
}

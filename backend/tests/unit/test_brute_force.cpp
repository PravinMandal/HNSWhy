#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>

#include "engine/brute_force.h"
#include "framework/test_framework.h"

namespace {

hnswhy::VectorStore make_store() {
    hnswhy::VectorStore store;
    const std::array<float, 2> a{0.0f, 0.0f};
    const std::array<float, 2> b{1.0f, 0.0f};
    const std::array<float, 2> c{0.0f, 2.0f};
    store.add(std::span<const float>(a));
    store.add(std::span<const float>(b));
    store.add(std::span<const float>(c));
    return store;
}

}  // namespace

HNSWHY_TEST(bruteforce_returns_nearest_first) {
    const hnswhy::VectorStore store = make_store();
    const hnswhy::BruteForce brute_force(store);
    const std::array<float, 2> query{0.9f, 0.0f};

    const hnswhy::SearchResult result = brute_force.search(std::span<const float>(query), 2);
    CHECK_EQ(result.ids.size(), std::size_t{2});
    CHECK_EQ(result.ids[0], hnswhy::Slot{1});
    CHECK_EQ(result.ids[1], hnswhy::Slot{0});
    CHECK_NEAR(result.distances[0], 0.01, 1e-6);
    CHECK_NEAR(result.distances[1], 0.81, 1e-6);
    CHECK_EQ(result.stats.distance_computations, std::uint64_t{3});
}

HNSWHY_TEST(bruteforce_breaks_ties_by_slot) {
    hnswhy::VectorStore store;
    const std::array<float, 2> a{1.0f, 0.0f};
    const std::array<float, 2> b{-1.0f, 0.0f};
    store.add(std::span<const float>(a));
    store.add(std::span<const float>(b));

    const hnswhy::BruteForce brute_force(store);
    const std::array<float, 2> query{0.0f, 0.0f};
    const hnswhy::SearchResult result = brute_force.search(std::span<const float>(query), 2);

    CHECK_EQ(result.ids[0], hnswhy::Slot{0});
    CHECK_EQ(result.ids[1], hnswhy::Slot{1});
    CHECK_NEAR(result.distances[0], 1.0, 0.0);
    CHECK_NEAR(result.distances[1], 1.0, 0.0);
}

HNSWHY_TEST(bruteforce_caps_k_at_store_size) {
    const hnswhy::VectorStore store = make_store();
    const hnswhy::BruteForce brute_force(store);
    const std::array<float, 2> query{0.0f, 0.0f};

    const hnswhy::SearchResult result = brute_force.search(std::span<const float>(query), 10);
    CHECK_EQ(result.ids.size(), std::size_t{3});
    CHECK_EQ(result.distances.size(), std::size_t{3});
}

HNSWHY_TEST(bruteforce_k_zero_is_empty) {
    const hnswhy::VectorStore store = make_store();
    const hnswhy::BruteForce brute_force(store);
    const std::array<float, 2> query{0.0f, 0.0f};

    CHECK(brute_force.search(std::span<const float>(query), 0).ids.empty());
}

HNSWHY_TEST(bruteforce_empty_store_returns_empty_for_any_query) {
    // An empty store has no dimension yet. Asking it for neighbours is not a
    // dimension error; it simply has nothing to return, like HNSW::search.
    const hnswhy::VectorStore store;
    const hnswhy::BruteForce brute_force(store);
    const std::array<float, 3> query{1.0f, 2.0f, 3.0f};

    const hnswhy::SearchResult result = brute_force.search(std::span<const float>(query), 5);
    CHECK(result.ids.empty());
    CHECK(result.distances.empty());
    CHECK_EQ(result.stats.distance_computations, std::uint64_t{0});
}

HNSWHY_TEST(bruteforce_rejects_query_dimension_mismatch) {
    const hnswhy::VectorStore store = make_store();
    const hnswhy::BruteForce brute_force(store);
    const std::array<float, 3> query{0.0f, 0.0f, 0.0f};
    CHECK_THROWS_AS(brute_force.search(std::span<const float>(query), 1), std::invalid_argument);
}

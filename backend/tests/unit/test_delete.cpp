#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

#include "engine/brute_force.h"
#include "engine/hnsw.h"
#include "engine/metrics.h"
#include "framework/test_framework.h"

namespace {

vf::VectorStore make_random_store(std::size_t n, std::size_t dim, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    std::vector<float> row(dim);
    vf::VectorStore store;
    store.reserve(n, dim);
    for (std::size_t i = 0; i < n; ++i) {
        for (float& value : row) {
            value = uniform(rng);
        }
        store.add(std::span<const float>(row));
    }
    return store;
}

vf::HNSWParams params() {
    vf::HNSWParams p;
    p.M = 8;
    p.M0 = 16;
    p.efConstruction = 100;
    p.seed = 2024;
    return p;
}

}  // namespace

VF_TEST(hnsw_deleted_nodes_are_never_returned) {
    const vf::VectorStore store = make_random_store(200, 6, 21);
    vf::HNSW hnsw(store, params());
    hnsw.build();

    for (vf::Slot id = 0; id < 100; ++id) {
        hnsw.remove(id);
    }
    CHECK_EQ(hnsw.size(), std::size_t{100});
    CHECK_EQ(hnsw.slot_count(), std::size_t{200});
    hnsw.validate();

    std::mt19937 rng(8);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    for (int q = 0; q < 20; ++q) {
        std::vector<float> query(store.dim());
        for (float& value : query) {
            value = uniform(rng);
        }
        const vf::SearchResult result = hnsw.search(query, 10, 60);
        CHECK_EQ(result.ids.size(), std::size_t{10});
        for (const vf::Slot id : result.ids) {
            CHECK(id >= 100);
            CHECK(!hnsw.isDeleted(id));
        }
    }
}

VF_TEST(hnsw_search_works_after_the_entry_point_is_deleted) {
    const vf::VectorStore store = make_random_store(180, 5, 22);
    vf::HNSW hnsw(store, params());
    hnsw.build();

    const vf::Slot entry = hnsw.entryPoint();
    hnsw.remove(entry);
    hnsw.validate();

    const vf::BruteForce brute_force(store);
    std::mt19937 rng(9);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    for (int q = 0; q < 20; ++q) {
        std::vector<float> query(store.dim());
        for (float& value : query) {
            value = uniform(rng);
        }
        const vf::SearchResult truth = brute_force.search(query, 5);
        const vf::SearchResult result = hnsw.search(query, 5, 50);
        CHECK_EQ(result.ids.size(), std::size_t{5});
        CHECK(std::find(result.ids.begin(), result.ids.end(), entry) == result.ids.end());
        CHECK(vf::tie_aware_recall_at_k(result.distances, truth.distances, 5) >= 0.8);
    }
}

VF_TEST(hnsw_can_insert_after_every_node_is_deleted) {
    vf::VectorStore store = make_random_store(80, 4, 23);
    vf::HNSW hnsw(store, params());
    hnsw.build();

    for (vf::Slot id = 0; id < store.size(); ++id) {
        hnsw.remove(id);
    }
    CHECK_EQ(hnsw.size(), std::size_t{0});
    hnsw.validate();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(hnsw.search(query, 5, 20).ids.empty());

    const std::array<float, 4> fresh{0.0f, 0.0f, 0.0f, 0.0f};
    const vf::Slot new_id = store.add(std::span<const float>(fresh));
    CHECK_EQ(new_id, vf::Slot{80});
    hnsw.insert(new_id);
    hnsw.validate();

    CHECK_EQ(hnsw.size(), std::size_t{1});
    const vf::SearchResult result = hnsw.search(query, 3, 20);
    CHECK_EQ(result.ids.size(), std::size_t{1});
    CHECK_EQ(result.ids[0], new_id);
    CHECK_NEAR(result.distances[0], 0.0, 0.0);
}

VF_TEST(hnsw_remove_is_idempotent) {
    const vf::VectorStore store = make_random_store(20, 3, 24);
    vf::HNSW hnsw(store, params());
    hnsw.build();

    hnsw.remove(3);
    hnsw.remove(3);
    CHECK_EQ(hnsw.size(), std::size_t{19});
    CHECK(hnsw.isDeleted(3));
}

VF_TEST(hnsw_remove_rejects_out_of_range) {
    const vf::VectorStore store = make_random_store(10, 3, 25);
    vf::HNSW hnsw(store, params());
    hnsw.build();

    CHECK_THROWS_AS(hnsw.remove(10), std::out_of_range);
    CHECK_THROWS_AS(hnsw.isDeleted(10), std::out_of_range);
}

VF_TEST(hnsw_all_deleted_index_returns_less_than_k) {
    const vf::VectorStore store = make_random_store(30, 3, 26);
    vf::HNSW hnsw(store, params());
    hnsw.build();
    for (vf::Slot id = 0; id < store.size(); ++id) {
        hnsw.remove(id);
    }

    const std::vector<float> query{0.0f, 0.0f, 0.0f};
    const vf::SearchResult result = hnsw.search(query, 10, 30);
    CHECK(result.ids.empty());
    CHECK(result.distances.empty());
}

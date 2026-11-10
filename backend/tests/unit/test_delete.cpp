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

hnswhy::VectorStore make_random_store(std::size_t n, std::size_t dim, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    std::vector<float> row(dim);
    hnswhy::VectorStore store;
    store.reserve(n, dim);
    for (std::size_t i = 0; i < n; ++i) {
        for (float& value : row) {
            value = uniform(rng);
        }
        store.add(std::span<const float>(row));
    }
    return store;
}

hnswhy::HNSWParams params() {
    hnswhy::HNSWParams p;
    p.M = 8;
    p.M0 = 16;
    p.efConstruction = 100;
    p.seed = 2024;
    return p;
}

hnswhy::VectorStore live_store(const hnswhy::VectorStore& store, const hnswhy::HNSW& graph) {
    hnswhy::VectorStore live;
    live.reserve(store.size() - graph.size(), store.dim());
    for (hnswhy::Slot id = 0; id < store.size(); ++id) {
        if (!graph.isDeleted(id)) {
            live.add(store.get(id));
        }
    }
    return live;
}

}  // namespace

HNSWHY_TEST(hnsw_deleted_nodes_are_never_returned) {
    const hnswhy::VectorStore store = make_random_store(200, 6, 21);
    hnswhy::HNSW hnsw(store, params());
    hnsw.build();

    for (hnswhy::Slot id = 0; id < 100; ++id) {
        hnsw.remove(id);
    }
    CHECK_EQ(hnsw.size(), std::size_t{100});
    CHECK_EQ(hnsw.slot_count(), std::size_t{200});
    hnsw.validate();

    const hnswhy::VectorStore live = live_store(store, hnsw);
    const hnswhy::BruteForce brute_force(live);
    std::mt19937 rng(8);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    for (int q = 0; q < 20; ++q) {
        std::vector<float> query(store.dim());
        for (float& value : query) {
            value = uniform(rng);
        }
        const hnswhy::SearchResult result = hnsw.search(query, 10, 60);
        const hnswhy::SearchResult truth = brute_force.search(query, 10);
        CHECK_EQ(result.ids.size(), std::size_t{10});
        for (const hnswhy::Slot id : result.ids) {
            CHECK(id >= 100);
            CHECK(!hnsw.isDeleted(id));
        }
        CHECK(hnswhy::tie_aware_recall_at_k(result.distances, truth.distances, 10) >= 0.8);
    }
}

HNSWHY_TEST(hnsw_search_works_after_the_entry_point_is_deleted) {
    const hnswhy::VectorStore store = make_random_store(180, 5, 22);
    hnswhy::HNSW hnsw(store, params());
    hnsw.build();

    const hnswhy::Slot entry = hnsw.entryPoint();
    hnsw.remove(entry);
    hnsw.validate();

    const hnswhy::VectorStore live = live_store(store, hnsw);
    const hnswhy::BruteForce brute_force(live);
    std::mt19937 rng(9);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    for (int q = 0; q < 20; ++q) {
        std::vector<float> query(store.dim());
        for (float& value : query) {
            value = uniform(rng);
        }
        const hnswhy::SearchResult truth = brute_force.search(query, 5);
        const hnswhy::SearchResult result = hnsw.search(query, 5, 50);
        CHECK_EQ(result.ids.size(), std::size_t{5});
        CHECK(std::find(result.ids.begin(), result.ids.end(), entry) == result.ids.end());
        CHECK(hnswhy::tie_aware_recall_at_k(result.distances, truth.distances, 5) >= 0.9);
    }
}

HNSWHY_TEST(hnsw_can_insert_after_every_node_is_deleted) {
    hnswhy::VectorStore store = make_random_store(80, 4, 23);
    hnswhy::HNSW hnsw(store, params());
    hnsw.build();

    for (hnswhy::Slot id = 0; id < store.size(); ++id) {
        hnsw.remove(id);
    }
    CHECK_EQ(hnsw.size(), std::size_t{0});
    hnsw.validate();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(hnsw.search(query, 5, 20).ids.empty());

    const std::array<float, 4> fresh{0.0f, 0.0f, 0.0f, 0.0f};
    const hnswhy::Slot new_id = store.add(std::span<const float>(fresh));
    CHECK_EQ(new_id, hnswhy::Slot{80});
    hnsw.insert(new_id);
    hnsw.validate();

    CHECK_EQ(hnsw.size(), std::size_t{1});
    const hnswhy::SearchResult result = hnsw.search(query, 3, 20);
    CHECK_EQ(result.ids.size(), std::size_t{1});
    CHECK_EQ(result.ids[0], new_id);
    CHECK_NEAR(result.distances[0], 0.0, 0.0);
}

HNSWHY_TEST(hnsw_remove_is_idempotent) {
    const hnswhy::VectorStore store = make_random_store(20, 3, 24);
    hnswhy::HNSW hnsw(store, params());
    hnsw.build();

    hnsw.remove(3);
    hnsw.remove(3);
    CHECK_EQ(hnsw.size(), std::size_t{19});
    CHECK(hnsw.isDeleted(3));
}

HNSWHY_TEST(hnsw_remove_rejects_out_of_range) {
    const hnswhy::VectorStore store = make_random_store(10, 3, 25);
    hnswhy::HNSW hnsw(store, params());
    hnsw.build();

    CHECK_THROWS_AS(hnsw.remove(10), std::out_of_range);
    CHECK_THROWS_AS(hnsw.isDeleted(10), std::out_of_range);
}

HNSWHY_TEST(hnsw_all_deleted_index_returns_less_than_k) {
    const hnswhy::VectorStore store = make_random_store(30, 3, 26);
    hnswhy::HNSW hnsw(store, params());
    hnsw.build();
    for (hnswhy::Slot id = 0; id < store.size(); ++id) {
        hnsw.remove(id);
    }

    const std::vector<float> query{0.0f, 0.0f, 0.0f};
    const hnswhy::SearchResult result = hnsw.search(query, 10, 30);
    CHECK(result.ids.empty());
    CHECK(result.distances.empty());
}

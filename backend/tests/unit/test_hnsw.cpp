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

std::vector<float> random_query(std::size_t dim, std::mt19937& rng) {
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    std::vector<float> query(dim);
    for (float& value : query) {
        value = uniform(rng);
    }
    return query;
}

vf::HNSWParams small_params() {
    vf::HNSWParams params;
    params.M = 8;
    params.M0 = 16;
    params.efConstruction = 100;
    params.seed = 123;
    return params;
}

}  // namespace

VF_TEST(hnsw_empty_index_returns_empty) {
    vf::VectorStore store;
    store.reserve(1, 4);
    vf::HNSW hnsw(store, small_params());
    hnsw.validate();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f};
    const vf::SearchResult result = hnsw.search(query, 5, 10);
    CHECK(result.ids.empty());
    CHECK(result.distances.empty());
    CHECK_EQ(hnsw.size(), std::size_t{0});
    CHECK_EQ(hnsw.topLevel(), -1);
}

VF_TEST(hnsw_finds_exact_neighbours_on_small_random_set) {
    const vf::VectorStore store = make_random_store(200, 8, 11);
    vf::HNSW hnsw(store, small_params());
    hnsw.build();
    hnsw.validate();
    CHECK_EQ(hnsw.size(), store.size());

    const vf::BruteForce brute_force(store);
    std::mt19937 query_rng(99);
    double recall_sum = 0.0;
    constexpr std::size_t k = 10;
    constexpr std::size_t query_count = 25;
    for (std::size_t q = 0; q < query_count; ++q) {
        const std::vector<float> query = random_query(store.dim(), query_rng);
        const vf::SearchResult truth = brute_force.search(query, k);
        const vf::SearchResult result = hnsw.search(query, k, 80);
        CHECK_EQ(result.ids.size(), k);
        CHECK_EQ(result.distances.size(), k);
        for (std::size_t i = 1; i < result.distances.size(); ++i) {
            CHECK(result.distances[i - 1] <= result.distances[i]);
        }
        recall_sum += vf::tie_aware_recall_at_k(result.distances, truth.distances, k);
    }

    const double mean_recall = recall_sum / static_cast<double>(query_count);
    CHECK(mean_recall >= 0.95);
}

VF_TEST(hnsw_self_queries_return_the_point_first) {
    const vf::VectorStore store = make_random_store(150, 6, 12);
    vf::HNSW hnsw(store, small_params());
    hnsw.build();

    std::size_t self_hits = 0;
    for (vf::Slot id = 0; id < store.size(); ++id) {
        const vf::SearchResult result = hnsw.search(store.get(id), 1, 50);
        CHECK_EQ(result.ids.size(), std::size_t{1});
        if (result.ids[0] == id) {
            ++self_hits;
        }
    }
    CHECK(self_hits >= 145);
}

VF_TEST(hnsw_handles_duplicate_vectors) {
    constexpr std::size_t n = 64;
    constexpr std::size_t dim = 4;
    std::vector<float> zero(dim, 0.0f);
    vf::VectorStore store;
    store.reserve(n, dim);
    for (std::size_t i = 0; i < n; ++i) {
        store.add(std::span<const float>(zero));
    }

    vf::HNSW hnsw(store, small_params());
    hnsw.build();
    hnsw.validate();

    const vf::SearchResult result = hnsw.search(zero, 10, 32);
    CHECK_EQ(result.ids.size(), std::size_t{10});
    for (const float distance : result.distances) {
        CHECK_NEAR(distance, 0.0, 0.0);
    }
}

VF_TEST(hnsw_is_deterministic_for_the_same_seed) {
    const vf::VectorStore store = make_random_store(120, 5, 13);
    vf::HNSW first(store, small_params());
    vf::HNSW second(store, small_params());
    first.build();
    second.build();

    std::mt19937 query_rng(5);
    for (int q = 0; q < 10; ++q) {
        const std::vector<float> query = random_query(store.dim(), query_rng);
        const vf::SearchResult a = first.search(query, 5, 40);
        const vf::SearchResult b = second.search(query, 5, 40);
        CHECK_EQ(a.ids.size(), b.ids.size());
        for (std::size_t i = 0; i < a.ids.size(); ++i) {
            CHECK_EQ(a.ids[i], b.ids[i]);
            CHECK_NEAR(a.distances[i], b.distances[i], 0.0);
        }
    }
}

VF_TEST(hnsw_ef_below_k_still_returns_k) {
    const vf::VectorStore store = make_random_store(100, 4, 14);
    vf::HNSW hnsw(store, small_params());
    hnsw.build();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f};
    const vf::SearchResult result = hnsw.search(query, 10, 1);
    CHECK_EQ(result.ids.size(), std::size_t{10});
}

VF_TEST(hnsw_survives_permuted_insertion_order) {
    constexpr std::size_t n = 240;
    constexpr std::size_t dim = 6;
    const vf::VectorStore original = make_random_store(n, dim, 15);

    std::mt19937 rng(77);
    std::vector<vf::Slot> order(n);
    for (vf::Slot i = 0; i < n; ++i) {
        order[i] = i;
    }
    std::shuffle(order.begin(), order.end(), rng);

    vf::VectorStore permuted;
    permuted.reserve(n, dim);
    for (const vf::Slot id : order) {
        permuted.add(original.get(id));
    }

    vf::HNSW hnsw(permuted, small_params());
    hnsw.build();
    hnsw.validate();

    const vf::BruteForce brute_force(permuted);
    double recall_sum = 0.0;
    constexpr std::size_t query_count = 20;
    for (std::size_t q = 0; q < query_count; ++q) {
        const std::span<const float> query = original.get(static_cast<vf::Slot>(q * 7));
        const vf::SearchResult truth = brute_force.search(query, 10);
        const vf::SearchResult result = hnsw.search(query, 10, 80);
        recall_sum += vf::tie_aware_recall_at_k(result.distances, truth.distances, 10);
    }
    CHECK(recall_sum / static_cast<double>(query_count) >= 0.95);
}

VF_TEST(hnsw_rejects_invalid_parameters) {
    vf::VectorStore store;
    store.reserve(1, 4);

    vf::HNSWParams params;
    params.M = 2;
    CHECK_THROWS_AS(vf::HNSW(store, params), std::invalid_argument);

    params = small_params();
    params.M0 = params.M - 1;
    CHECK_THROWS_AS(vf::HNSW(store, params), std::invalid_argument);

    params = small_params();
    params.efConstruction = 0;
    CHECK_THROWS_AS(vf::HNSW(store, params), std::invalid_argument);

    params = small_params();
    params.maxLevel = 256;
    CHECK_THROWS_AS(vf::HNSW(store, params), std::invalid_argument);
}

VF_TEST(hnsw_rejects_dimension_mismatch_query) {
    const vf::VectorStore store = make_random_store(20, 4, 16);
    vf::HNSW hnsw(store, small_params());
    hnsw.build();

    const std::array<float, 3> query{0.0f, 0.0f, 0.0f};
    CHECK_THROWS_AS(hnsw.search(query, 3, 10), std::invalid_argument);
}

VF_TEST(hnsw_reports_search_stats) {
    const vf::VectorStore store = make_random_store(150, 6, 18);
    vf::HNSW hnsw(store, small_params());
    hnsw.build();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const vf::SearchResult result = hnsw.search(query, 5, 40);
    CHECK(result.stats.distance_computations > 0);
    CHECK(result.stats.hops > 0);
    CHECK_EQ(result.stats.layer_visits, static_cast<std::uint32_t>(hnsw.topLevel() + 1));
}

VF_TEST(hnsw_rejects_out_of_store_insert) {
    const vf::VectorStore store = make_random_store(6, 3, 17);
    vf::HNSW hnsw(store, small_params());
    hnsw.insert(0);
    CHECK_THROWS_AS(hnsw.insert(2), std::logic_error);
    CHECK_THROWS_AS(hnsw.insert(6), std::out_of_range);
}

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

std::vector<float> random_query(std::size_t dim, std::mt19937& rng) {
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    std::vector<float> query(dim);
    for (float& value : query) {
        value = uniform(rng);
    }
    return query;
}

hnswhy::HNSWParams small_params() {
    hnswhy::HNSWParams params;
    params.M = 8;
    params.M0 = 16;
    params.efConstruction = 100;
    params.seed = 123;
    return params;
}

}  // namespace

HNSWHY_TEST(hnsw_empty_index_returns_empty) {
    hnswhy::VectorStore store;
    store.reserve(1, 4);
    hnswhy::HNSW hnsw(store, small_params());
    hnsw.validate();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f};
    const hnswhy::SearchResult result = hnsw.search(query, 5, 10);
    CHECK(result.ids.empty());
    CHECK(result.distances.empty());
    CHECK_EQ(hnsw.size(), std::size_t{0});
    CHECK_EQ(hnsw.topLevel(), -1);
}

HNSWHY_TEST(hnsw_finds_exact_neighbours_on_small_random_set) {
    const hnswhy::VectorStore store = make_random_store(200, 8, 11);
    hnswhy::HNSW hnsw(store, small_params());
    hnsw.build();
    hnsw.validate();
    CHECK_EQ(hnsw.size(), store.size());

    const hnswhy::BruteForce brute_force(store);
    std::mt19937 query_rng(99);
    double recall_sum = 0.0;
    constexpr std::size_t k = 10;
    constexpr std::size_t query_count = 25;
    for (std::size_t q = 0; q < query_count; ++q) {
        const std::vector<float> query = random_query(store.dim(), query_rng);
        const hnswhy::SearchResult truth = brute_force.search(query, k);
        const hnswhy::SearchResult result = hnsw.search(query, k, 80);
        CHECK_EQ(result.ids.size(), k);
        CHECK_EQ(result.distances.size(), k);
        for (std::size_t i = 1; i < result.distances.size(); ++i) {
            CHECK(result.distances[i - 1] <= result.distances[i]);
        }
        recall_sum += hnswhy::tie_aware_recall_at_k(result.distances, truth.distances, k);
        // The exact-scan fallback returns before visiting any layer. A nonzero
        // count proves the query really walked the graph, and no store
        // completion was needed to fill the result.
        CHECK(result.stats.layer_visits > 0);
        CHECK_EQ(result.stats.exact_fallbacks, std::uint32_t{0});
    }

    const double mean_recall = recall_sum / static_cast<double>(query_count);
    CHECK(mean_recall >= 0.95);
}

HNSWHY_TEST(hnsw_self_queries_return_the_point_first) {
    const hnswhy::VectorStore store = make_random_store(150, 6, 12);
    hnswhy::HNSW hnsw(store, small_params());
    hnsw.build();

    std::size_t self_hits = 0;
    for (hnswhy::Slot id = 0; id < store.size(); ++id) {
        const hnswhy::SearchResult result = hnsw.search(store.get(id), 1, 50);
        CHECK_EQ(result.ids.size(), std::size_t{1});
        CHECK_EQ(result.stats.exact_fallbacks, std::uint32_t{0});
        if (result.ids[0] == id) {
            ++self_hits;
        }
    }
    CHECK(self_hits >= 145);
}

HNSWHY_TEST(hnsw_handles_duplicate_vectors) {
    constexpr std::size_t n = 64;
    constexpr std::size_t dim = 4;
    std::vector<float> zero(dim, 0.0f);
    hnswhy::VectorStore store;
    store.reserve(n, dim);
    for (std::size_t i = 0; i < n; ++i) {
        store.add(std::span<const float>(zero));
    }

    hnswhy::HNSW hnsw(store, small_params());
    hnsw.build();
    hnsw.validate();

    const hnswhy::SearchResult result = hnsw.search(zero, 10, 32);
    CHECK_EQ(result.ids.size(), std::size_t{10});
    for (const float distance : result.distances) {
        CHECK_NEAR(distance, 0.0, 0.0);
    }
}

HNSWHY_TEST(hnsw_tie_heavy_search_returns_all_live_vectors) {
    // More duplicates than M0 + 1: the graph cannot keep every copy reachable
    // with a bounded degree, so search completes the short result from the
    // store instead of silently returning a truncated list.
    constexpr std::size_t n = 64;
    constexpr std::size_t dim = 4;
    const std::vector<float> zero(dim, 0.0f);
    hnswhy::VectorStore duplicates;
    duplicates.reserve(n, dim);
    for (std::size_t i = 0; i < n; ++i) {
        duplicates.add(zero);
    }
    hnswhy::HNSW duplicate_graph(duplicates, small_params());
    duplicate_graph.build();
    duplicate_graph.validate();
    const hnswhy::SearchResult all_duplicates = duplicate_graph.search(zero, n, n);
    CHECK_EQ(all_duplicates.ids.size(), n);
    CHECK(all_duplicates.stats.exact_fallbacks > 0);
    for (const float distance : all_duplicates.distances) {
        CHECK_NEAR(distance, 0.0, 0.0);
    }

    // One-hot vectors are equidistant from each other. The query matches one
    // exact axis; a truncated graph result must still be completed.
    hnswhy::VectorStore one_hot;
    one_hot.reserve(n, n + 4);
    std::vector<float> row(n + 4, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        std::fill(row.begin(), row.end(), 0.0f);
        row[i] = 1.0f;
        one_hot.add(row);
    }
    hnswhy::HNSW one_hot_graph(one_hot, small_params());
    one_hot_graph.build();
    const hnswhy::SearchResult all_one_hot = one_hot_graph.search(one_hot.get(0), n, n);
    CHECK_EQ(all_one_hot.ids.size(), n);
    CHECK(all_one_hot.stats.exact_fallbacks > 0);
    CHECK_EQ(all_one_hot.ids.front(), hnswhy::Slot{0});
    CHECK_NEAR(all_one_hot.distances.front(), 0.0, 0.0);
}

HNSWHY_TEST(hnsw_incremental_duplicates_stay_searchable) {
    constexpr std::size_t base_count = 48;
    constexpr std::size_t duplicates = 32;
    constexpr std::size_t dim = 8;
    const hnswhy::VectorStore base = make_random_store(base_count, dim, 21);

    hnswhy::VectorStore store;
    store.reserve(base_count + duplicates, dim);
    for (hnswhy::Slot id = 0; id < base_count; ++id) {
        store.add(base.get(id));
    }
    hnswhy::HNSW graph(store, small_params());
    graph.build();
    graph.validate();

    // Append exact copies one at a time, as incremental uploads would. Each
    // copy must stay reachable instead of becoming a one-way leaf that only an
    // exact scan could find.
    for (std::size_t i = 0; i < duplicates; ++i) {
        store.add(base.get(0));
        graph.insert(static_cast<hnswhy::Slot>(base_count + i));
    }
    graph.validate();
    CHECK_EQ(graph.reachableCount(0), graph.size());

    const hnswhy::SearchResult result = graph.search(base.get(0), 20, 64);
    CHECK_EQ(result.ids.size(), std::size_t{20});
    CHECK_EQ(result.stats.exact_fallbacks, std::uint32_t{0});
    std::size_t zero_distances = 0;
    for (const float distance : result.distances) {
        if (distance == 0.0f) {
            ++zero_distances;
        }
    }
    CHECK_EQ(zero_distances, std::size_t{20});
}

HNSWHY_TEST(hnsw_removing_unreachable_nodes_restores_graph_search) {
    constexpr std::size_t n = 64;
    constexpr std::size_t dim = n + 4;
    hnswhy::VectorStore store;
    store.reserve(n, dim);
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<float> row(dim, 0.0f);
        row[i] = 1.0f;
        store.add(row);
    }
    hnswhy::HNSW graph(store, small_params());
    graph.build();
    CHECK(graph.reachableCount(0) < graph.size());

    // One-hot vectors build a disconnected layer 0. Tombstone the trailing
    // unreachable nodes until the live set is connected again.
    while (graph.size() > 1 && graph.reachableCount(0) != graph.size()) {
        graph.remove(static_cast<hnswhy::Slot>(graph.size() - 1));
    }
    CHECK_EQ(graph.reachableCount(0), graph.size());

    const hnswhy::SearchResult result = graph.search(store.get(0), 1, 64);
    CHECK_EQ(result.ids.size(), std::size_t{1});
    CHECK_EQ(result.ids.front(), hnswhy::Slot{0});
    // If the disconnected flag stayed stuck, search would scan every live
    // vector and return before visiting any layer. A connected graph must take
    // the approximate path instead.
    CHECK(result.stats.layer_visits > 0);
    CHECK_EQ(result.stats.exact_fallbacks, std::uint32_t{0});
}

HNSWHY_TEST(hnsw_incremental_insert_after_connected_removals_rechecks_reachability) {
    // Regression: an insertion can prune an edge or attach the new node to a
    // tombstoned component, leaving layer 0 disconnected while the cached
    // "connected" flag still says true. Search would then skip the exact
    // fallback and return a wrong neighbour for the new node.
    hnswhy::VectorStore store = make_random_store(40, 4, 6942);
    hnswhy::HNSWParams params;
    params.M = 4;
    params.M0 = 4;
    params.efConstruction = 8;
    params.seed = 6942;
    hnswhy::HNSW graph(store, params);
    graph.build();
    CHECK(graph.reachableCount(0) < graph.size());

    // Tombstone the trailing unreachable nodes until the live graph is
    // connected again, then search once so remove()'s dirty flag is cleared.
    // The bug needs the cached flag to be true at insertion time.
    hnswhy::Slot removed = hnswhy::kInvalidSlot;
    while (graph.size() > 1 && graph.reachableCount(0) != graph.size()) {
        removed = static_cast<hnswhy::Slot>(graph.size() - 1);
        graph.remove(removed);
    }
    CHECK_EQ(graph.reachableCount(0), graph.size());
    graph.search(store.get(0), 1, 50);

    // Re-insert a copy of a removed vector. Its nearest candidate is the
    // tombstone, which can accept a back-edge while being outside the live
    // component.
    store.add(store.get(removed));
    const hnswhy::Slot added = static_cast<hnswhy::Slot>(store.size() - 1);
    graph.insert(added);

    // If the insert left the graph disconnected, the next search must notice
    // and use the exact fallback. A connected result needs no fallback.
    const bool disconnected = graph.reachableCount(0) != graph.size();
    const hnswhy::SearchResult result = graph.search(store.get(added), 1, 50);
    CHECK_EQ(result.ids.size(), std::size_t{1});
    CHECK_EQ(result.ids.front(), added);
    CHECK_EQ(result.stats.exact_fallbacks > 0, disconnected);
}

HNSWHY_TEST(hnsw_is_deterministic_for_the_same_seed) {
    const hnswhy::VectorStore store = make_random_store(120, 5, 13);
    hnswhy::HNSW first(store, small_params());
    hnswhy::HNSW second(store, small_params());
    first.build();
    second.build();

    std::mt19937 query_rng(5);
    for (int q = 0; q < 10; ++q) {
        const std::vector<float> query = random_query(store.dim(), query_rng);
        const hnswhy::SearchResult a = first.search(query, 5, 40);
        const hnswhy::SearchResult b = second.search(query, 5, 40);
        CHECK_EQ(a.ids.size(), b.ids.size());
        for (std::size_t i = 0; i < a.ids.size(); ++i) {
            CHECK_EQ(a.ids[i], b.ids[i]);
            CHECK_NEAR(a.distances[i], b.distances[i], 0.0);
        }
    }
}

HNSWHY_TEST(hnsw_ef_below_k_still_returns_k) {
    const hnswhy::VectorStore store = make_random_store(100, 4, 14);
    hnswhy::HNSW hnsw(store, small_params());
    hnsw.build();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f};
    const hnswhy::SearchResult result = hnsw.search(query, 10, 1);
    CHECK_EQ(result.ids.size(), std::size_t{10});
    CHECK_EQ(result.stats.exact_fallbacks, std::uint32_t{0});
}

HNSWHY_TEST(hnsw_zero_k_returns_empty_and_ef_zero_still_returns_k) {
    const hnswhy::VectorStore store = make_random_store(100, 4, 14);
    hnswhy::HNSW hnsw(store, small_params());
    hnsw.build();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(hnsw.search(query, 0, 10).ids.empty());

    // ef is clamped up to k, so ef=0 is not an error; it means "use k".
    const hnswhy::SearchResult result = hnsw.search(query, 10, 0);
    CHECK_EQ(result.ids.size(), std::size_t{10});
}

HNSWHY_TEST(hnsw_survives_permuted_insertion_order) {
    constexpr std::size_t n = 240;
    constexpr std::size_t dim = 6;
    const hnswhy::VectorStore original = make_random_store(n, dim, 15);

    std::mt19937 rng(77);
    std::vector<hnswhy::Slot> order(n);
    for (hnswhy::Slot i = 0; i < n; ++i) {
        order[i] = i;
    }
    std::shuffle(order.begin(), order.end(), rng);

    hnswhy::VectorStore permuted;
    permuted.reserve(n, dim);
    for (const hnswhy::Slot id : order) {
        permuted.add(original.get(id));
    }

    hnswhy::HNSW hnsw(permuted, small_params());
    hnsw.build();
    hnsw.validate();

    const hnswhy::BruteForce brute_force(permuted);
    double recall_sum = 0.0;
    constexpr std::size_t query_count = 20;
    for (std::size_t q = 0; q < query_count; ++q) {
        const std::span<const float> query = original.get(static_cast<hnswhy::Slot>(q * 7));
        const hnswhy::SearchResult truth = brute_force.search(query, 10);
        const hnswhy::SearchResult result = hnsw.search(query, 10, 80);
        CHECK_EQ(result.stats.exact_fallbacks, std::uint32_t{0});
        recall_sum += hnswhy::tie_aware_recall_at_k(result.distances, truth.distances, 10);
    }
    CHECK(recall_sum / static_cast<double>(query_count) >= 0.95);
}

HNSWHY_TEST(hnsw_rejects_invalid_parameters) {
    hnswhy::VectorStore store;
    store.reserve(1, 4);

    hnswhy::HNSWParams params;
    params.M = 2;
    CHECK_THROWS_AS(hnswhy::HNSW(store, params), std::invalid_argument);

    params = small_params();
    params.M0 = params.M - 1;
    CHECK_THROWS_AS(hnswhy::HNSW(store, params), std::invalid_argument);

    params = small_params();
    params.efConstruction = 0;
    CHECK_THROWS_AS(hnswhy::HNSW(store, params), std::invalid_argument);

    params = small_params();
    params.maxLevel = 256;
    CHECK_THROWS_AS(hnswhy::HNSW(store, params), std::invalid_argument);

    // M0 is stored in a uint16_t, so the constructor must reject values that
    // would silently truncate.
    params = small_params();
    params.M0 = 65536;
    CHECK_THROWS_AS(hnswhy::HNSW(store, params), std::invalid_argument);
}

HNSWHY_TEST(hnsw_rejects_dimension_mismatch_query) {
    const hnswhy::VectorStore store = make_random_store(20, 4, 16);
    hnswhy::HNSW hnsw(store, small_params());
    hnsw.build();

    const std::array<float, 3> query{0.0f, 0.0f, 0.0f};
    CHECK_THROWS_AS(hnsw.search(query, 3, 10), std::invalid_argument);
}

HNSWHY_TEST(hnsw_reports_search_stats) {
    const hnswhy::VectorStore store = make_random_store(150, 6, 18);
    hnswhy::HNSW hnsw(store, small_params());
    hnsw.build();

    const std::vector<float> query{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const hnswhy::SearchResult result = hnsw.search(query, 5, 40);
    CHECK(result.stats.distance_computations > 0);
    CHECK(result.stats.hops > 0);
    CHECK_EQ(result.stats.layer_visits, static_cast<std::uint32_t>(hnsw.topLevel() + 1));
    CHECK_EQ(result.stats.exact_fallbacks, std::uint32_t{0});
}

HNSWHY_TEST(hnsw_rejects_out_of_store_insert) {
    const hnswhy::VectorStore store = make_random_store(6, 3, 17);
    hnswhy::HNSW hnsw(store, small_params());
    hnsw.insert(0);
    CHECK_THROWS_AS(hnsw.insert(2), std::logic_error);
    CHECK_THROWS_AS(hnsw.insert(6), std::out_of_range);
}

HNSWHY_TEST(hnsw_back_edge_prune_keeps_the_new_node) {
    // A full neighbour list must be pruned with the new back-edge in the pool.
    // Pruning the old list first and appending unconditionally evicts the new
    // node and loses an exact self-query.
    constexpr std::size_t dim = 34;
    hnswhy::VectorStore store;
    store.reserve(34, dim);
    store.add(std::vector<float>(dim, 0.0f));
    for (std::size_t axis = 0; axis < 32; ++axis) {
        std::vector<float> row(dim, 0.0f);
        row[axis] = 1.0f;
        store.add(row);
    }
    std::vector<float> combination(dim, 0.0f);
    combination[0] = 0.5f;
    combination[1] = 0.1f;
    store.add(combination);

    hnswhy::HNSWParams params;
    params.M = 16;
    params.M0 = 32;
    params.efConstruction = 200;
    params.seed = 42;
    hnswhy::HNSW graph(store, params);
    graph.build();
    graph.validate();

    const hnswhy::SearchResult result = graph.search(store.get(32), 1, 64);
    CHECK_EQ(result.ids.size(), std::size_t{1});
    CHECK_EQ(result.ids.front(), hnswhy::Slot{32});
    CHECK_NEAR(result.distances.front(), 0.0, 0.0);
}

HNSWHY_TEST(hnsw_empty_default_store_returns_empty) {
    hnswhy::VectorStore store;
    hnswhy::HNSW graph(store, small_params());
    const std::vector<float> query{1.0f, 2.0f, 3.0f};
    const hnswhy::SearchResult result = graph.search(query, 5, 10);
    CHECK(result.ids.empty());
    CHECK(result.distances.empty());
}

HNSWHY_TEST(hnsw_one_hot_self_queries_stay_exact_and_prefix_consistent) {
    // Equidistant one-hot vectors create a disconnected layer-0 graph. Search
    // must detect that and fall back to an exact scan, otherwise a high-id
    // node's exact self-query can return an unrelated point.
    constexpr std::size_t n = 64;
    constexpr std::size_t dim = n + 4;
    hnswhy::VectorStore store;
    store.reserve(n, dim);
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<float> row(dim, 0.0f);
        row[i] = 1.0f;
        store.add(row);
    }
    hnswhy::HNSW graph(store, small_params());
    graph.build();
    graph.validate();

    for (const hnswhy::Slot id : {hnswhy::Slot{0}, hnswhy::Slot{16}, hnswhy::Slot{63}}) {
        const hnswhy::SearchResult one = graph.search(store.get(id), 1, 64);
        CHECK_EQ(one.ids.size(), std::size_t{1});
        CHECK_EQ(one.ids.front(), id);
        CHECK_NEAR(one.distances.front(), 0.0, 0.0);
        // This graph is deliberately disconnected, so the exact scan is the
        // documented path rather than a silent degradation.
        CHECK(one.stats.exact_fallbacks > 0);

        const hnswhy::SearchResult ten = graph.search(store.get(id), 10, 64);
        CHECK_EQ(ten.ids.size(), std::size_t{10});
        CHECK_EQ(ten.ids.front(), id);
        CHECK_EQ(ten.ids.front(), one.ids.front());
    }
}

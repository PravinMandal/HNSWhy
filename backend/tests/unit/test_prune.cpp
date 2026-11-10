#include <array>
#include <cstddef>
#include <random>
#include <span>
#include <vector>

#include "engine/hnsw.h"
#include "framework/test_framework.h"

namespace {

// ref at (0,0), A on the x-axis, B farther along it, D off the axis.
hnswhy::VectorStore make_diverse_store() {
    hnswhy::VectorStore store;
    const std::array<float, 2> ref{0.0f, 0.0f};
    const std::array<float, 2> a{1.0f, 0.0f};
    const std::array<float, 2> b{2.0f, 0.0f};
    const std::array<float, 2> d{0.0f, 5.0f};
    store.add(std::span<const float>(ref));
    store.add(std::span<const float>(a));
    store.add(std::span<const float>(b));
    store.add(std::span<const float>(d));
    return store;
}

// Two separated Gaussian clusters. A correct diversity prune keeps a bridge
// between them; nearest-only pruning can let the far cluster fall out of the
// entry point's reachable component.
hnswhy::VectorStore make_two_cluster_store(std::size_t per_cluster) {
    hnswhy::VectorStore store;
    std::mt19937 rng(7);
    std::normal_distribution<float> jitter(0.0f, 0.6f);
    for (std::size_t i = 0; i < per_cluster; ++i) {
        const std::array<float, 2> point{jitter(rng), jitter(rng)};
        store.add(std::span<const float>(point));
    }
    for (std::size_t i = 0; i < per_cluster; ++i) {
        const std::array<float, 2> point{100.0f + jitter(rng), jitter(rng)};
        store.add(std::span<const float>(point));
    }
    return store;
}

std::vector<hnswhy::Candidate> candidates_for_ref() {
    return {hnswhy::Candidate{1.0f, 1}, hnswhy::Candidate{4.0f, 2}, hnswhy::Candidate{25.0f, 3}};
}

}  // namespace

HNSWHY_TEST(heuristic_keeps_diverse_neighbour_instead_of_near_duplicate) {
    const hnswhy::VectorStore store = make_diverse_store();
    const hnswhy::HNSW hnsw(store, hnswhy::HNSWParams{});

    const std::vector<hnswhy::Candidate> candidates = candidates_for_ref();
    const std::vector<hnswhy::Slot> selected = hnsw.selectNeighbours(0, candidates, 2);

    CHECK_EQ(selected.size(), std::size_t{2});
    CHECK_EQ(selected[0], hnswhy::Slot{1});
    CHECK_EQ(selected[1], hnswhy::Slot{3});
}

HNSWHY_TEST(nearest_only_keeps_the_two_closest) {
    const hnswhy::VectorStore store = make_diverse_store();
    hnswhy::HNSWParams params;
    params.prune = hnswhy::PruneStrategy::NearestOnly;
    const hnswhy::HNSW hnsw(store, params);

    const std::vector<hnswhy::Candidate> candidates = candidates_for_ref();
    const std::vector<hnswhy::Slot> selected = hnsw.selectNeighbours(0, candidates, 2);

    CHECK_EQ(selected.size(), std::size_t{2});
    CHECK_EQ(selected[0], hnswhy::Slot{1});
    CHECK_EQ(selected[1], hnswhy::Slot{2});
}

HNSWHY_TEST(heuristic_keeps_equal_distance_duplicates) {
    const hnswhy::VectorStore store = make_diverse_store();
    const hnswhy::HNSW hnsw(store, hnswhy::HNSWParams{});

    // Slot 0 (ref) and slot 1 (A) are one unit apart. A duplicate of A has the
    // same distance to ref as to the selected A, so it is not strictly redundant
    // and is kept; otherwise duplicates would become unreachable leaves.
    hnswhy::VectorStore with_duplicate;
    const std::array<float, 2> ref{0.0f, 0.0f};
    const std::array<float, 2> a{1.0f, 0.0f};
    with_duplicate.add(std::span<const float>(ref));
    with_duplicate.add(std::span<const float>(a));
    with_duplicate.add(std::span<const float>(a));

    const hnswhy::HNSW duplicate_graph(with_duplicate, hnswhy::HNSWParams{});
    const std::vector<hnswhy::Candidate> candidates{
        hnswhy::Candidate{1.0f, 1},
        hnswhy::Candidate{1.0f, 2},
    };
    const std::vector<hnswhy::Slot> selected = duplicate_graph.selectNeighbours(0, candidates, 3);
    CHECK_EQ(selected.size(), std::size_t{2});
    CHECK_EQ(selected[0], hnswhy::Slot{1});
    CHECK_EQ(selected[1], hnswhy::Slot{2});

    const std::vector<hnswhy::Slot> capped = hnsw.selectNeighbours(0, candidates_for_ref(), 0);
    CHECK(capped.empty());
}

HNSWHY_TEST(heuristic_skips_ref_and_duplicate_candidate_ids) {
    const hnswhy::VectorStore store = make_diverse_store();
    const hnswhy::HNSW hnsw(store, hnswhy::HNSWParams{});

    const std::vector<hnswhy::Candidate> candidates{
        hnswhy::Candidate{0.0f, 0},
        hnswhy::Candidate{1.0f, 1},
        hnswhy::Candidate{1.0f, 1},
    };
    const std::vector<hnswhy::Slot> selected = hnsw.selectNeighbours(0, candidates, 4);
    CHECK_EQ(selected.size(), std::size_t{1});
    CHECK_EQ(selected[0], hnswhy::Slot{1});
}

HNSWHY_TEST(two_cluster_heuristic_keeps_both_clusters_reachable) {
    constexpr std::size_t per_cluster = 120;
    const hnswhy::VectorStore store = make_two_cluster_store(per_cluster);

    hnswhy::HNSWParams params;
    params.M = 8;
    params.M0 = 16;
    params.efConstruction = 100;
    params.seed = 7;

    hnswhy::HNSW heuristic(store, params);
    heuristic.build();
    heuristic.validate();
    const std::size_t heuristic_reached = heuristic.reachableCount(0);

    hnswhy::HNSWParams nearest_params = params;
    nearest_params.prune = hnswhy::PruneStrategy::NearestOnly;
    hnswhy::HNSW nearest(store, nearest_params);
    nearest.build();
    nearest.validate();
    const std::size_t nearest_reached = nearest.reachableCount(0);

    const auto total = static_cast<double>(per_cluster * 2);
    CHECK(heuristic_reached >= static_cast<std::size_t>(0.9 * total));
    // Nearest-only may still reach the entry cluster in full; the property under
    // test is that the far cluster is unreachable, so at most one cluster is seen.
    CHECK(nearest_reached <= per_cluster);
}

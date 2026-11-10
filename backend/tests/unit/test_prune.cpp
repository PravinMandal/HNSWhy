#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "engine/hnsw.h"
#include "framework/test_framework.h"

namespace {

// ref at (0,0), A on the x-axis, B farther along it, D off the axis.
vf::VectorStore make_diverse_store() {
    vf::VectorStore store;
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

std::vector<vf::Candidate> candidates_for_ref() {
    return {vf::Candidate{1.0f, 1}, vf::Candidate{4.0f, 2}, vf::Candidate{25.0f, 3}};
}

}  // namespace

VF_TEST(heuristic_keeps_diverse_neighbour_instead_of_near_duplicate) {
    const vf::VectorStore store = make_diverse_store();
    const vf::HNSW hnsw(store, vf::HNSWParams{});

    const std::vector<vf::Candidate> candidates = candidates_for_ref();
    const std::vector<vf::Slot> selected = hnsw.selectNeighbours(0, candidates, 2);

    CHECK_EQ(selected.size(), std::size_t{2});
    CHECK_EQ(selected[0], vf::Slot{1});
    CHECK_EQ(selected[1], vf::Slot{3});
}

VF_TEST(nearest_only_keeps_the_two_closest) {
    const vf::VectorStore store = make_diverse_store();
    vf::HNSWParams params;
    params.prune = vf::PruneStrategy::NearestOnly;
    const vf::HNSW hnsw(store, params);

    const std::vector<vf::Candidate> candidates = candidates_for_ref();
    const std::vector<vf::Slot> selected = hnsw.selectNeighbours(0, candidates, 2);

    CHECK_EQ(selected.size(), std::size_t{2});
    CHECK_EQ(selected[0], vf::Slot{1});
    CHECK_EQ(selected[1], vf::Slot{2});
}

VF_TEST(heuristic_keeps_equal_distance_duplicates) {
    const vf::VectorStore store = make_diverse_store();
    const vf::HNSW hnsw(store, vf::HNSWParams{});

    // Slot 0 (ref) and slot 1 (A) are one unit apart. A duplicate of A has the
    // same distance to ref as to the selected A, so it is not strictly redundant
    // and is kept; otherwise duplicates would become unreachable leaves.
    vf::VectorStore with_duplicate;
    const std::array<float, 2> ref{0.0f, 0.0f};
    const std::array<float, 2> a{1.0f, 0.0f};
    with_duplicate.add(std::span<const float>(ref));
    with_duplicate.add(std::span<const float>(a));
    with_duplicate.add(std::span<const float>(a));

    const vf::HNSW duplicate_graph(with_duplicate, vf::HNSWParams{});
    const std::vector<vf::Candidate> candidates{
        vf::Candidate{1.0f, 1},
        vf::Candidate{1.0f, 2},
    };
    const std::vector<vf::Slot> selected = duplicate_graph.selectNeighbours(0, candidates, 3);
    CHECK_EQ(selected.size(), std::size_t{2});
    CHECK_EQ(selected[0], vf::Slot{1});
    CHECK_EQ(selected[1], vf::Slot{2});

    const std::vector<vf::Slot> capped = hnsw.selectNeighbours(0, candidates_for_ref(), 0);
    CHECK(capped.empty());
}

VF_TEST(heuristic_skips_ref_and_duplicate_candidate_ids) {
    const vf::VectorStore store = make_diverse_store();
    const vf::HNSW hnsw(store, vf::HNSWParams{});

    const std::vector<vf::Candidate> candidates{
        vf::Candidate{0.0f, 0},
        vf::Candidate{1.0f, 1},
        vf::Candidate{1.0f, 1},
    };
    const std::vector<vf::Slot> selected = hnsw.selectNeighbours(0, candidates, 4);
    CHECK_EQ(selected.size(), std::size_t{1});
    CHECK_EQ(selected[0], vf::Slot{1});
}

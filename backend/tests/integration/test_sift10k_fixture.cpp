#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <vector>

#include "bench/fvecs.h"
#include "engine/brute_force.h"
#include "engine/hnsw.h"
#include "engine/metrics.h"
#include "framework/test_framework.h"

#ifndef HNSWHY_FIXTURE_DIR
#error "HNSWHY_FIXTURE_DIR must point at tests/fixtures/sift10k"
#endif

// The fixture is committed so a clean clone can run a real-data gate without
// downloading SIFT. Ground truth was generated over this exact 10k prefix.
HNSWHY_TEST(sift10k_fixture_bruteforce_matches_groundtruth) {
    const std::filesystem::path root(HNSWHY_FIXTURE_DIR);
    const hnswhy::VectorStore base = hnswhy::load_fvecs_store(root / "base_10k.fvecs");
    const hnswhy::FloatMatrix queries = hnswhy::load_fvecs(root / "queries_1k.fvecs");
    const hnswhy::IntMatrix truth_ids = hnswhy::load_ivecs(root / "groundtruth_ids.ivecs");
    const hnswhy::FloatMatrix truth_distances = hnswhy::load_fvecs(root / "groundtruth_dist.fvecs");

    CHECK_EQ(base.size(), std::size_t{10000});
    CHECK_EQ(base.dim(), std::size_t{128});
    CHECK_EQ(queries.n, std::size_t{1000});
    CHECK_EQ(queries.dim, std::size_t{128});
    CHECK_EQ(truth_ids.n, std::size_t{1000});
    CHECK_EQ(truth_ids.dim, std::size_t{100});
    CHECK_EQ(truth_distances.n, std::size_t{1000});
    CHECK_EQ(truth_distances.dim, std::size_t{100});

    const hnswhy::BruteForce brute_force(base);
    const std::size_t measured = 100;
    std::size_t id_matches = 0;
    std::size_t total = 0;
    double max_distance_error = 0.0;

    for (std::size_t q = 0; q < measured; ++q) {
        const std::span<const float> query = queries.row(q);
        const std::span<const std::int32_t> expected_ids = truth_ids.row(q);
        const std::span<const float> expected_distances = truth_distances.row(q);
        const hnswhy::SearchResult result = brute_force.search(query, expected_ids.size());
        CHECK_EQ(result.ids.size(), expected_ids.size());

        const double recall1 =
            hnswhy::tie_aware_recall_at_k(result.distances, expected_distances, 1);
        const double recall10 =
            hnswhy::tie_aware_recall_at_k(result.distances, expected_distances, 10);
        CHECK_NEAR(recall1, 1.0, 1e-12);
        CHECK_NEAR(recall10, 1.0, 1e-12);

        for (std::size_t i = 0; i < expected_ids.size(); ++i) {
            const std::int32_t expected = expected_ids[i];
            CHECK(expected >= 0);
            CHECK(static_cast<std::size_t>(expected) < base.size());
            max_distance_error =
                std::max(max_distance_error,
                         std::fabs(static_cast<double>(result.distances[i]) -
                                   static_cast<double>(expected_distances[i])));
            if (std::find(result.ids.begin(), result.ids.end(), static_cast<hnswhy::Slot>(expected)) !=
                result.ids.end()) {
                ++id_matches;
            }
        }
        total += expected_ids.size();
    }

    const double id_overlap = static_cast<double>(id_matches) / static_cast<double>(total);
    std::cout << "  sift10k fixture: id overlap " << id_overlap
              << ", max |distance error| " << max_distance_error << "\n";

    CHECK(id_overlap >= 0.99);
    CHECK(max_distance_error < 1e-3);
}

// A real-data recall gate on the committed prefix. The fixture exists so CI can
// check the graph against exact ground truth without downloading SIFT. The gate
// is deliberately loose; the headline SIFT-100k run reports the precise number.
HNSWHY_TEST(sift10k_fixture_hnsw_meets_recall_gate) {
    const std::filesystem::path root(HNSWHY_FIXTURE_DIR);
    const hnswhy::VectorStore base = hnswhy::load_fvecs_store(root / "base_10k.fvecs");
    const hnswhy::FloatMatrix queries = hnswhy::load_fvecs(root / "queries_1k.fvecs");
    const hnswhy::FloatMatrix truth_distances = hnswhy::load_fvecs(root / "groundtruth_dist.fvecs");

    hnswhy::HNSWParams params;
    hnswhy::HNSW index(base, params);
    index.build();
    CHECK_EQ(index.size(), base.size());

    double recall10_sum = 0.0;
    double recall1_sum = 0.0;
    for (std::size_t q = 0; q < queries.n; ++q) {
        const hnswhy::SearchResult result = index.search(queries.row(q), 10, 50);
        CHECK_EQ(result.ids.size(), std::size_t{10});
        recall1_sum += hnswhy::tie_aware_recall_at_k(result.distances, truth_distances.row(q), 1);
        recall10_sum += hnswhy::tie_aware_recall_at_k(result.distances, truth_distances.row(q), 10);
    }
    const double mean_r1 = recall1_sum / static_cast<double>(queries.n);
    const double mean_r10 = recall10_sum / static_cast<double>(queries.n);
    std::cout << "  sift10k fixture HNSW: R@1 " << mean_r1 << ", R@10 " << mean_r10 << "\n";

    CHECK(mean_r1 >= 0.90);
    CHECK(mean_r10 >= 0.90);
    index.validate();
}

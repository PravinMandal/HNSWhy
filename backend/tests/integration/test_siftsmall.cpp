#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>

#include "bench/fvecs.h"
#include "engine/brute_force.h"
#include "engine/distance.h"
#include "framework/test_framework.h"

// Independent gate: exact search over siftsmall must reproduce the shipped
// answer key. The shipped file contains IDs only, so the true k-th distance is
// recovered from those IDs and recall is scored tie-aware.
HNSWHY_TEST(siftsmall_bruteforce_matches_shipped_groundtruth) {
    const char* dir = std::getenv("HNSWHY_SIFTSMALL_DIR");
    if (dir == nullptr || *dir == '\0') {
        throw hnswhytest::Skip("HNSWHY_SIFTSMALL_DIR is not set");
    }

    const std::filesystem::path root(dir);
    const hnswhy::VectorStore base = hnswhy::load_fvecs_store(root / "siftsmall_base.fvecs");
    const hnswhy::FloatMatrix queries = hnswhy::load_fvecs(root / "siftsmall_query.fvecs");
    const hnswhy::IntMatrix truth = hnswhy::load_ivecs(root / "siftsmall_groundtruth.ivecs");

    CHECK_EQ(base.size(), std::size_t{10000});
    CHECK_EQ(base.dim(), std::size_t{128});
    CHECK_EQ(queries.n, std::size_t{100});
    CHECK_EQ(queries.dim, std::size_t{128});
    CHECK_EQ(truth.n, std::size_t{100});
    CHECK_EQ(truth.dim, std::size_t{100});

    const hnswhy::BruteForce brute_force(base);
    const std::size_t k = truth.dim;
    std::size_t hits = 0;
    std::size_t total = 0;
    std::size_t id_matches = 0;
    double max_kth_error = 0.0;

    for (std::size_t q = 0; q < queries.n; ++q) {
        const std::span<const float> query = queries.row(q);
        const hnswhy::SearchResult result = brute_force.search(query, k);
        CHECK_EQ(result.ids.size(), k);

        const std::span<const std::int32_t> truth_ids = truth.row(q);
        float kth_truth = 0.0f;
        for (const std::int32_t truth_id : truth_ids) {
            CHECK(truth_id >= 0);
            CHECK(static_cast<std::size_t>(truth_id) < base.size());
            kth_truth = std::max(
                kth_truth, hnswhy::squared_l2(query, base.get(static_cast<hnswhy::Slot>(truth_id))));
        }

        const float tie_eps = 1e-5f * std::max(1.0f, kth_truth);
        for (const float distance : result.distances) {
            if (distance <= kth_truth + tie_eps) {
                ++hits;
            }
        }
        total += k;

        for (const hnswhy::Slot id : result.ids) {
            if (std::find(truth_ids.begin(), truth_ids.end(), static_cast<std::int32_t>(id)) !=
                truth_ids.end()) {
                ++id_matches;
            }
        }

        max_kth_error = std::max<double>(
            max_kth_error, std::fabs(static_cast<double>(result.distances.back()) - kth_truth));
        CHECK(result.distances.back() <= kth_truth + tie_eps);
        CHECK(kth_truth <= result.distances.back() + tie_eps);
    }

    const double distance_recall = static_cast<double>(hits) / static_cast<double>(total);
    const double id_overlap = static_cast<double>(id_matches) / static_cast<double>(total);

    std::cout << "  siftsmall: tie-aware recall " << distance_recall
              << ", id overlap " << id_overlap << ", max |kth error| " << max_kth_error << "\n";

    CHECK_EQ(hits, total);
    CHECK(id_overlap >= 0.99);
}

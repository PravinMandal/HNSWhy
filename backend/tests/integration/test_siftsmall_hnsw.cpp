#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>

#include "bench/fvecs.h"
#include "engine/distance.h"
#include "engine/hnsw.h"
#include "engine/metrics.h"
#include "framework/test_framework.h"

// Real-data gate for the graph: build siftsmall, then score against the shipped
// exact answer key. Ground truth stores IDs only, so the cutoff distance comes
// from the base vectors.
VF_TEST(siftsmall_hnsw_recall_and_invariants) {
    const char* dir = std::getenv("VF_SIFTSMALL_DIR");
    if (dir == nullptr || *dir == '\0') {
        std::cout << "  [skip] VF_SIFTSMALL_DIR is not set\n";
        return;
    }

    const std::filesystem::path root(dir);
    const vf::VectorStore base = vf::load_fvecs_store(root / "siftsmall_base.fvecs");
    const vf::FloatMatrix queries = vf::load_fvecs(root / "siftsmall_query.fvecs");
    const vf::IntMatrix truth = vf::load_ivecs(root / "siftsmall_groundtruth.ivecs");

    vf::HNSWParams params;
    params.M = 16;
    params.M0 = 32;
    params.efConstruction = 200;
    params.seed = 42;

    const auto build_start = std::chrono::steady_clock::now();
    vf::HNSW hnsw(base, params);
    hnsw.build();
    const auto build_end = std::chrono::steady_clock::now();
    hnsw.validate();
    CHECK_EQ(hnsw.size(), base.size());

    double recall1_sum = 0.0;
    double recall10_sum = 0.0;
    std::vector<double> latencies_ms;
    latencies_ms.reserve(queries.n);

    for (std::size_t q = 0; q < queries.n; ++q) {
        const std::span<const float> query = queries.row(q);
        const std::span<const std::int32_t> truth_ids = truth.row(q);

        std::vector<float> truth_distances;
        truth_distances.reserve(truth_ids.size());
        for (const std::int32_t truth_id : truth_ids) {
            CHECK(truth_id >= 0);
            CHECK(static_cast<std::size_t>(truth_id) < base.size());
            truth_distances.push_back(
                vf::squared_l2(query, base.get(static_cast<vf::Slot>(truth_id))));
        }

        const auto query_start = std::chrono::steady_clock::now();
        const vf::SearchResult result = hnsw.search(query, 10, 50);
        const auto query_end = std::chrono::steady_clock::now();
        latencies_ms.push_back(
            std::chrono::duration<double, std::milli>(query_end - query_start).count());

        CHECK_EQ(result.ids.size(), std::size_t{10});
        recall1_sum += vf::tie_aware_recall_at_k(result.distances, truth_distances, 1);
        recall10_sum += vf::tie_aware_recall_at_k(result.distances, truth_distances, 10);
    }

    const double recall1 = recall1_sum / static_cast<double>(queries.n);
    const double recall10 = recall10_sum / static_cast<double>(queries.n);
    const double build_ms =
        std::chrono::duration<double, std::milli>(build_end - build_start).count();
    const vf::LatencySummary latency = vf::summarize_latency(latencies_ms);

    std::cout << "  siftsmall hnsw: build " << build_ms << " ms, R@1 " << recall1 << ", R@10 "
              << recall10 << ", p50 " << latency.p50 << " ms, p95 " << latency.p95 << " ms\n";

    CHECK(recall1 >= 0.95);
    CHECK(recall10 >= 0.90);
    CHECK(hnsw.distanceComputations() > 0);
}

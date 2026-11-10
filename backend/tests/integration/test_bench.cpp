#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "bench/report.h"
#include "framework/test_framework.h"
#include "json.hpp"

namespace {

struct TempPath {
    std::filesystem::path path;

    explicit TempPath(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("hnswhy_bench_" + name)) {
        std::filesystem::remove(path);
    }

    ~TempPath() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

}  // namespace

HNSWHY_TEST(bench_report_writes_stable_json_schema) {
    hnswhy::BenchReport report;
    report.dataset.name = "unit-fixture";
    report.dataset.base_file = "base.fvecs";
    report.dataset.base_count = 123;
    report.dataset.base_bytes = 456;
    report.dataset.dim = 8;
    report.dataset.queries_file = "queries.fvecs";
    report.dataset.query_count = 7;
    report.dataset.queries_bytes = 89;
    report.dataset.ground_truth_file = "gt.ivecs";
    report.dataset.ground_truth_k = 10;

    report.config.M = 16;
    report.config.M0 = 32;
    report.config.ef_construction = 200;
    report.config.seed = 42;
    report.config.k = 10;
    report.config.passes = 3;
    report.config.warmup_queries = 200;
    report.config.build_type = "release";

    report.build.build_time_ms = 12.5;
    report.build.rss_after_build_mb = 34.5;
    report.build.peak_rss_mb = 40.0;

    report.provenance.run_id = "20261111T000000Z-abc1234";
    report.provenance.git_commit = "abc1234";
    report.provenance.worktree_dirty = true;
    report.provenance.base_prefix = 123;
    report.provenance.base_sha256 = "base-hash";
    report.provenance.queries_sha256 = "queries-hash";
    report.provenance.ground_truth_ids_sha256 = "ids-hash";
    report.provenance.ground_truth_distances_sha256 = "dist-hash";
    report.provenance.cxx_flags_release = "-O3 -DNDEBUG";

    hnswhy::MethodReport hnsw;
    hnsw.name = "hnsw";
    hnsw.ef_search = 50;
    hnsw.queries_measured = 21;
    hnsw.recall_at_1 = 0.99;
    hnsw.recall_at_10 = 0.98;
    hnsw.qps = 1234.5;
    hnsw.latency_ms = {0.1, 0.2, 0.3, 0.4};
    hnsw.avg_distance_computations = 800.0;
    report.methods.push_back(hnsw);

    hnswhy::MethodReport exact;
    exact.name = "bruteforce";
    exact.queries_measured = 7;
    exact.recall_at_1 = 1.0;
    exact.recall_at_10 = 1.0;
    exact.qps = 10.0;
    exact.latency_ms = {100.0, 100.0, 100.0, 100.0};
    exact.avg_distance_computations = 123.0;
    report.methods.push_back(exact);

    TempPath output("report.json");
    hnswhy::write_bench_json(output.path, report);

    std::ifstream in(output.path);
    CHECK(static_cast<bool>(in));
    nlohmann::json root;
    in >> root;

    CHECK_EQ(root.at("schema_version").get<int>(), 1);
    CHECK_EQ(root.at("dataset").at("name").get<std::string>(), std::string("unit-fixture"));
    CHECK_EQ(root.at("dataset").at("base_count").get<std::size_t>(), std::size_t{123});
    CHECK_EQ(root.at("config").at("M").get<std::size_t>(), std::size_t{16});
    CHECK_EQ(root.at("build").at("build_time_ms").get<double>(), 12.5);
    CHECK_EQ(root.at("methods").size(), std::size_t{2});
    CHECK_EQ(root.at("methods")[0].at("name").get<std::string>(), std::string("hnsw"));
    CHECK_EQ(root.at("methods")[0].at("ef_search").get<std::size_t>(), std::size_t{50});
    CHECK_EQ(root.at("methods")[0].at("latency_ms").at("p95").get<double>(), 0.3);
    CHECK(!root.at("methods")[1].contains("ef_search"));
    CHECK(root.at("environment").contains("cpu"));
    CHECK_EQ(root.at("provenance").at("run_id").get<std::string>(),
             std::string("20261111T000000Z-abc1234"));
    CHECK_EQ(root.at("provenance").at("worktree_dirty").get<bool>(), true);
    CHECK_EQ(root.at("provenance").at("base_prefix").get<std::size_t>(), std::size_t{123});
    CHECK_EQ(root.at("provenance").at("cxx_flags_release").get<std::string>(),
             std::string("-O3 -DNDEBUG"));
}

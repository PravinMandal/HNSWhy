#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/metrics.h"

namespace hnswhy {

struct DatasetReport {
    std::string name;
    std::string base_file;
    std::size_t base_count = 0;
    std::size_t dim = 0;
    std::uintmax_t base_bytes = 0;
    std::string queries_file;
    std::size_t query_count = 0;
    std::uintmax_t queries_bytes = 0;
    std::string ground_truth_file;
    std::size_t ground_truth_k = 0;
};

struct BuildReport {
    double build_time_ms = 0.0;
    double rss_after_build_mb = 0.0;
    double peak_rss_mb = 0.0;
};

struct MethodReport {
    std::string name;
    std::size_t ef_search = 0;
    std::size_t queries_measured = 0;
    double recall_at_1 = 0.0;
    double recall_at_10 = 0.0;
    double qps = 0.0;
    LatencySummary latency_ms;
    double avg_distance_computations = 0.0;
};

struct BenchConfigReport {
    std::size_t M = 0;
    std::size_t M0 = 0;
    std::size_t ef_construction = 0;
    std::uint64_t seed = 0;
    std::size_t k = 0;
    std::size_t passes = 0;
    std::size_t warmup_queries = 0;
    std::string distance = "squared_l2";
    std::string build_type;
};

struct BenchReport {
    DatasetReport dataset;
    BenchConfigReport config;
    BuildReport build;
    std::vector<MethodReport> methods;
};

// Process memory from /proc (Linux). Returns 0 when unavailable.
double current_rss_mb();
double peak_rss_mb();

void write_bench_json(const std::filesystem::path& output, const BenchReport& report);

}  // namespace hnswhy

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
    // Loaded prefix bytes and the full source file size are different when the
    // benchmark uses only the first N vectors of a larger .fvecs file.
    std::uintmax_t base_bytes = 0;
    std::uintmax_t base_file_bytes = 0;
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

// Identifies the exact inputs and checkout that produced a report. The shell
// driver fills these values so the benchmark binary does not need to shell out
// to git or hash files itself.
struct ProvenanceReport {
    std::string run_id;
    std::string git_commit;
    bool worktree_dirty = false;
    std::size_t base_prefix = 0;
    std::string base_sha256;
    std::string queries_sha256;
    std::string ground_truth_ids_sha256;
    std::string ground_truth_distances_sha256;
    std::string cxx_flags_release;
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

// The exact baseline is also a correctness check: if brute force disagrees
// with the stored ground-truth distances, the whole report is untrustworthy.
struct ExactCheckReport {
    bool checked = false;
    double max_distance_error = 0.0;
    double tolerance = 1e-4;
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
    ProvenanceReport provenance;
    ExactCheckReport exact_check;
    std::vector<MethodReport> methods;
};

// Process memory from /proc (Linux). Returns 0 when unavailable.
double current_rss_mb();
double peak_rss_mb();

void write_bench_json(const std::filesystem::path& output, const BenchReport& report);

}  // namespace hnswhy

#include "bench/report.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/utsname.h>
#include <unistd.h>

#include "json.hpp"

namespace hnswhy {
namespace {

using json = nlohmann::json;

std::string now_utc() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string hostname() {
    char buffer[256] = {};
    if (gethostname(buffer, sizeof(buffer) - 1) != 0) {
        return {};
    }
    return buffer;
}

std::string cpu_model() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        const std::string key = "model name";
        if (line.rfind(key, 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                return {};
            }
            std::size_t start = line.find_first_not_of(" \t", colon + 1);
            if (start == std::string::npos) {
                return {};
            }
            return line.substr(start);
        }
    }
    return {};
}

std::string os_name() {
    struct utsname info {};
    if (uname(&info) != 0) {
        return {};
    }
    return std::string(info.sysname) + " " + info.release;
}

json latency_json(const LatencySummary& latency) {
    return json{{"mean", latency.mean}, {"p50", latency.p50}, {"p95", latency.p95},
                {"p99", latency.p99}};
}

}  // namespace

double current_rss_mb() {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::size_t value = 0;
            if (std::sscanf(line.c_str(), "VmRSS: %zu kB", &value) == 1) {
                return static_cast<double>(value) / 1024.0;
            }
        }
    }
    return 0.0;
}

double peak_rss_mb() {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            std::size_t value = 0;
            if (std::sscanf(line.c_str(), "VmHWM: %zu kB", &value) == 1) {
                return static_cast<double>(value) / 1024.0;
            }
        }
    }
    return 0.0;
}

void write_bench_json(const std::filesystem::path& output, const BenchReport& report) {
    json methods = json::array();
    for (const MethodReport& method : report.methods) {
        json entry{
            {"name", method.name},
            {"queries_measured", method.queries_measured},
            {"recall_at_1", method.recall_at_1},
            {"recall_at_10", method.recall_at_10},
            {"qps", method.qps},
            {"latency_ms", latency_json(method.latency_ms)},
            {"avg_distance_computations", method.avg_distance_computations},
        };
        if (method.ef_search != 0) {
            entry["ef_search"] = method.ef_search;
        }
        methods.push_back(std::move(entry));
    }

    json root{
        {"schema_version", 1},
        {"generated_at_utc", now_utc()},
        {"engine", {{"name", "HNSWhy"}, {"version", "0.1.0"}}},
        {"dataset",
         {{"name", report.dataset.name},
          {"base_file", report.dataset.base_file},
          {"base_count", report.dataset.base_count},
          {"base_bytes", report.dataset.base_bytes},
          {"dim", report.dataset.dim},
          {"queries_file", report.dataset.queries_file},
          {"query_count", report.dataset.query_count},
          {"queries_bytes", report.dataset.queries_bytes},
          {"ground_truth_file", report.dataset.ground_truth_file},
          {"ground_truth_k", report.dataset.ground_truth_k}}},
        {"config",
         {{"M", report.config.M},
          {"M0", report.config.M0},
          {"ef_construction", report.config.ef_construction},
          {"seed", report.config.seed},
          {"k", report.config.k},
          {"passes", report.config.passes},
          {"warmup_queries", report.config.warmup_queries},
          {"distance", report.config.distance},
          {"build_type", report.config.build_type}}},
        {"build",
         {{"build_time_ms", report.build.build_time_ms},
          {"rss_after_build_mb", report.build.rss_after_build_mb},
          {"peak_rss_mb", report.build.peak_rss_mb}}},
        {"methods", std::move(methods)},
        {"environment",
         {{"hostname", hostname()},
          {"os", os_name()},
          {"cpu", cpu_model()},
          {"hardware_threads", std::thread::hardware_concurrency()},
          {"compiler", __VERSION__},
          {"build_type",
#ifdef NDEBUG
           "release"
#else
           "debug"
#endif
          },
          {"avx2",
#ifdef __AVX2__
           true
#else
           false
#endif
          }}},
    };

    const std::filesystem::path parent = output.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream out(output);
    if (!out) {
        throw std::runtime_error("cannot write benchmark report: " + output.string());
    }
    out << root.dump(2) << '\n';
}

}  // namespace hnswhy

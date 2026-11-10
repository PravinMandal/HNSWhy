#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "bench/fvecs.h"
#include "bench/report.h"
#include "engine/brute_force.h"
#include "engine/hnsw.h"
#include "engine/metrics.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::filesystem::path base;
    std::filesystem::path queries;
    std::filesystem::path gt_ids;
    std::filesystem::path gt_dist;
    std::filesystem::path output = "docs/results/bench.json";
    std::string dataset_name = "SIFT-100k";
    std::size_t n = 0;
    std::size_t k = 10;
    std::size_t passes = 3;
    std::size_t warmup = 200;
    std::size_t M = 16;
    std::size_t M0 = 32;
    std::size_t ef_construction = 200;
    std::uint64_t seed = 42;
    std::vector<std::size_t> ef_grid{10, 20, 50, 100, 200};
    bool run_bruteforce = true;
};

[[noreturn]] void usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0
        << " --base FILE --queries FILE --gt-ids FILE --gt-dist FILE [options]\n"
        << "  --n N                 base prefix size (default: whole file)\n"
        << "  --k K                 neighbours per query (default 10)\n"
        << "  --ef LIST             comma-separated efSearch values (default 10,20,50,100,200)\n"
        << "  --passes P            timed passes per ef point (default 3)\n"
        << "  --warmup N            warmup queries per ef point (default 200)\n"
        << "  --M M --M0 M0         graph degree parameters (default 16 / 32)\n"
        << "  --ef-construction N   construction beam (default 200)\n"
        << "  --seed S              level RNG seed (default 42)\n"
        << "  --output FILE         JSON report path (default docs/results/bench.json)\n"
        << "  --dataset-name NAME   label stored in the report\n"
        << "  --no-bruteforce       skip the exact baseline\n";
    std::exit(2);
}

std::size_t parse_size(const std::string& text, const char* flag) {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
        throw std::invalid_argument(std::string("invalid value for ") + flag + ": " + text);
    }
    return static_cast<std::size_t>(value);
}

std::vector<std::size_t> parse_ef_list(const std::string& text) {
    std::vector<std::size_t> values;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string token =
            comma == std::string::npos ? text.substr(start) : text.substr(start, comma - start);
        if (token.empty()) {
            throw std::invalid_argument("empty efSearch value");
        }
        values.push_back(parse_size(token, "--ef"));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage(argv[0]);
            }
            return argv[++i];
        };
        if (arg == "--base") {
            options.base = value();
        } else if (arg == "--queries") {
            options.queries = value();
        } else if (arg == "--gt-ids") {
            options.gt_ids = value();
        } else if (arg == "--gt-dist") {
            options.gt_dist = value();
        } else if (arg == "--n") {
            options.n = parse_size(value(), "--n");
        } else if (arg == "--k") {
            options.k = parse_size(value(), "--k");
        } else if (arg == "--ef") {
            options.ef_grid = parse_ef_list(value());
        } else if (arg == "--passes") {
            options.passes = parse_size(value(), "--passes");
        } else if (arg == "--warmup") {
            options.warmup = parse_size(value(), "--warmup");
        } else if (arg == "--M") {
            options.M = parse_size(value(), "--M");
        } else if (arg == "--M0") {
            options.M0 = parse_size(value(), "--M0");
        } else if (arg == "--ef-construction") {
            options.ef_construction = parse_size(value(), "--ef-construction");
        } else if (arg == "--seed") {
            options.seed = static_cast<std::uint64_t>(parse_size(value(), "--seed"));
        } else if (arg == "--output") {
            options.output = value();
        } else if (arg == "--dataset-name") {
            options.dataset_name = value();
        } else if (arg == "--no-bruteforce") {
            options.run_bruteforce = false;
        } else {
            usage(argv[0]);
        }
    }

    if (options.base.empty() || options.queries.empty() || options.gt_ids.empty() ||
        options.gt_dist.empty()) {
        usage(argv[0]);
    }
    if (options.k == 0 || options.passes == 0 || options.ef_grid.empty()) {
        throw std::invalid_argument("k, passes, and efSearch must be positive");
    }
    return options;
}

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uintmax_t file_size_or_zero(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

void print_method(const vf::MethodReport& method) {
    std::cout << (method.name == "hnsw" ? "  ef=" + std::to_string(method.ef_search) + " "
                                        : "  exact  ")
              << "R@1=" << method.recall_at_1 << " R@10=" << method.recall_at_10
              << " QPS=" << method.qps << " p50=" << method.latency_ms.p50 << "ms"
              << " p95=" << method.latency_ms.p95 << "ms"
              << " p99=" << method.latency_ms.p99 << "ms"
              << " dist/q=" << method.avg_distance_computations << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse(argc, argv);

        std::cout << "[bench] loading base prefix\n";
        const vf::VectorStore base = vf::load_fvecs_store(options.base, options.n);
        const vf::FloatMatrix queries = vf::load_fvecs(options.queries);
        const vf::IntMatrix gt_ids = vf::load_ivecs(options.gt_ids);
        const vf::FloatMatrix gt_dist = vf::load_fvecs(options.gt_dist);

        if (queries.n == 0 || gt_ids.n != queries.n || gt_dist.n != queries.n) {
            throw std::runtime_error("query count and ground-truth count do not match");
        }
        if (gt_ids.dim != gt_dist.dim || gt_ids.dim < options.k) {
            throw std::runtime_error("ground truth does not cover k neighbours");
        }
        if (base.dim() != queries.dim) {
            throw std::runtime_error("base and query dimensions do not match");
        }
        for (std::size_t q = 0; q < gt_ids.n; ++q) {
            for (const std::int32_t id : gt_ids.row(q)) {
                if (id < 0 || static_cast<std::size_t>(id) >= base.size()) {
                    throw std::runtime_error(
                        "ground truth references a slot outside the loaded base prefix");
                }
            }
        }

        std::cout << "[bench] base " << base.size() << " x " << base.dim() << ", queries "
                  << queries.n << " x " << queries.dim << ", gt " << gt_ids.dim << "\n";

        vf::HNSWParams params;
        params.M = options.M;
        params.M0 = options.M0;
        params.efConstruction = options.ef_construction;
        params.seed = options.seed;

        vf::HNSW hnsw(base, params);
        const auto build_start = Clock::now();
        hnsw.build();
        const double build_ms = elapsed_ms(build_start);
        hnsw.validate();
        std::cout << "[bench] hnsw build " << build_ms << " ms, live " << hnsw.size()
                  << ", top level " << hnsw.topLevel() << "\n";

        vf::BenchReport report;
        report.dataset.name = options.dataset_name;
        report.dataset.base_file = options.base.string();
        report.dataset.base_count = base.size();
        report.dataset.base_bytes = file_size_or_zero(options.base);
        report.dataset.dim = base.dim();
        report.dataset.queries_file = options.queries.string();
        report.dataset.query_count = queries.n;
        report.dataset.queries_bytes = file_size_or_zero(options.queries);
        report.dataset.ground_truth_file = options.gt_ids.string();
        report.dataset.ground_truth_k = gt_ids.dim;

        report.config.M = params.M;
        report.config.M0 = params.M0;
        report.config.ef_construction = params.efConstruction;
        report.config.seed = params.seed;
        report.config.k = options.k;
        report.config.passes = options.passes;
        report.config.warmup_queries = options.warmup;
#ifdef NDEBUG
        report.config.build_type = "release";
#else
        report.config.build_type = "debug";
#endif

        report.build.build_time_ms = build_ms;
        report.build.rss_after_build_mb = vf::current_rss_mb();

        const std::size_t recall10_k = std::min<std::size_t>(10, options.k);

        for (const std::size_t ef : options.ef_grid) {
            for (std::size_t w = 0; w < std::min(options.warmup, queries.n); ++w) {
                (void)hnsw.search(queries.row(w), options.k, ef);
            }

            std::vector<double> latencies;
            latencies.reserve(queries.n * options.passes);
            double recall1_sum = 0.0;
            double recall10_sum = 0.0;
            double total_ms = 0.0;
            std::uint64_t distance_sum = 0;
            std::size_t measured = 0;

            for (std::size_t pass = 0; pass < options.passes; ++pass) {
                for (std::size_t q = 0; q < queries.n; ++q) {
                    const auto start = Clock::now();
                    const vf::SearchResult result = hnsw.search(queries.row(q), options.k, ef);
                    const double ms = elapsed_ms(start);
                    if (result.ids.size() != options.k) {
                        throw std::runtime_error("HNSW returned fewer than k results");
                    }
                    latencies.push_back(ms);
                    total_ms += ms;
                    distance_sum += result.stats.distance_computations;
                    recall1_sum += vf::tie_aware_recall_at_k(result.distances, gt_dist.row(q), 1);
                    recall10_sum +=
                        vf::tie_aware_recall_at_k(result.distances, gt_dist.row(q), recall10_k);
                    ++measured;
                }
            }

            vf::MethodReport method;
            method.name = "hnsw";
            method.ef_search = ef;
            method.queries_measured = measured;
            method.recall_at_1 = recall1_sum / static_cast<double>(measured);
            method.recall_at_10 = recall10_sum / static_cast<double>(measured);
            method.qps = total_ms > 0.0 ? static_cast<double>(measured) / (total_ms / 1000.0) : 0.0;
            method.latency_ms = vf::summarize_latency(std::move(latencies));
            method.avg_distance_computations =
                static_cast<double>(distance_sum) / static_cast<double>(measured);
            report.methods.push_back(method);
            print_method(method);
        }

        if (options.run_bruteforce) {
            std::cout << "[bench] exact baseline\n";
            const vf::BruteForce brute_force(base);
            for (std::size_t w = 0; w < std::min<std::size_t>(options.warmup, 20); ++w) {
                (void)brute_force.search(queries.row(w), options.k);
            }

            std::vector<double> latencies;
            latencies.reserve(queries.n);
            double recall1_sum = 0.0;
            double recall10_sum = 0.0;
            double total_ms = 0.0;
            double max_gt_distance_error = 0.0;
            std::uint64_t distance_sum = 0;

            const auto exact_start = Clock::now();
            for (std::size_t q = 0; q < queries.n; ++q) {
                const auto start = Clock::now();
                const vf::SearchResult result = brute_force.search(queries.row(q), options.k);
                const double ms = elapsed_ms(start);
                if (result.ids.size() != options.k) {
                    throw std::runtime_error("brute force returned fewer than k results");
                }
                latencies.push_back(ms);
                total_ms += ms;
                distance_sum += result.stats.distance_computations;
                recall1_sum += vf::tie_aware_recall_at_k(result.distances, gt_dist.row(q), 1);
                recall10_sum +=
                    vf::tie_aware_recall_at_k(result.distances, gt_dist.row(q), recall10_k);
                for (std::size_t i = 0; i < options.k; ++i) {
                    max_gt_distance_error =
                        std::max(max_gt_distance_error,
                                 static_cast<double>(
                                     std::fabs(result.distances[i] - gt_dist.row(q)[i])));
                }
                if ((q + 1) % 1000 == 0) {
                    std::cerr << "[bench] exact " << (q + 1) << "/" << queries.n << " ("
                              << seconds_since(exact_start) << "s)\n";
                }
            }

            if (max_gt_distance_error > 1e-4) {
                throw std::runtime_error("exact search disagrees with the ground-truth distances");
            }

            vf::MethodReport method;
            method.name = "bruteforce";
            method.queries_measured = queries.n;
            method.recall_at_1 = recall1_sum / static_cast<double>(queries.n);
            method.recall_at_10 = recall10_sum / static_cast<double>(queries.n);
            method.qps = total_ms > 0.0
                             ? static_cast<double>(queries.n) / (total_ms / 1000.0)
                             : 0.0;
            method.latency_ms = vf::summarize_latency(std::move(latencies));
            method.avg_distance_computations =
                static_cast<double>(distance_sum) / static_cast<double>(queries.n);
            report.methods.push_back(method);
            print_method(method);
        }

        report.build.peak_rss_mb = vf::peak_rss_mb();
        vf::write_bench_json(options.output, report);
        std::cout << "[bench] wrote " << options.output << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "hns_bench: " << error.what() << "\n";
        return 1;
    }
}

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "bench/fvecs.h"
#include "bench/ground_truth.h"

namespace {

struct Options {
    std::filesystem::path base;
    std::filesystem::path queries;
    std::filesystem::path out_ids;
    std::filesystem::path out_dist;
    std::size_t n = 0;
    std::size_t k = 100;
};

[[noreturn]] void usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " --base FILE --queries FILE"
              << " --out-ids FILE --out-dist FILE [--n N] [--k K]\n";
    std::exit(2);
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
        } else if (arg == "--out-ids") {
            options.out_ids = value();
        } else if (arg == "--out-dist") {
            options.out_dist = value();
        } else if (arg == "--n") {
            options.n = std::stoull(value());
        } else if (arg == "--k") {
            options.k = std::stoull(value());
        } else {
            usage(argv[0]);
        }
    }
    if (options.base.empty() || options.queries.empty() || options.out_ids.empty() ||
        options.out_dist.empty()) {
        usage(argv[0]);
    }
    return options;
}

void ensure_parent(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parse(argc, argv);

    try {
        const auto start = std::chrono::steady_clock::now();

        const vf::VectorStore base = vf::load_fvecs_store(options.base, options.n);
        const vf::FloatMatrix queries = vf::load_fvecs(options.queries);
        std::cerr << "[gt] base: " << base.size() << " x " << base.dim()
                  << ", queries: " << queries.n << " x " << queries.dim << "\n";

        std::size_t next_report = 0;
        const vf::GroundTruth truth = vf::compute_ground_truth(
            base, queries, options.k, [&](std::size_t done, std::size_t total) {
                if (done == total || done >= next_report) {
                    const double pct = 100.0 * static_cast<double>(done) / total;
                    std::cerr << "[gt] " << done << "/" << total << " (" << pct << "%)\n";
                    next_report = done + total / 10;
                }
            });

        ensure_parent(options.out_ids);
        ensure_parent(options.out_dist);
        vf::write_ivecs(options.out_ids, truth.ids, truth.n, truth.k);
        vf::write_fvecs(options.out_dist, truth.distances, truth.n, truth.k);

        const auto end = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(end - start).count();
        std::cerr << "[gt] wrote " << options.out_ids.string() << " and "
                  << options.out_dist.string() << " in " << seconds << "s\n";
    } catch (const std::exception& e) {
        std::cerr << "hns_gt: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

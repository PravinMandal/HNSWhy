#include "hnswhy/build_info.h"
#include "synthetic.h"

#include <charconv>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <system_error>

namespace {

bool parse_size(std::string_view text, std::size_t& value) {
    unsigned long long parsed = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

void print_usage(std::ostream& out) {
    out << "usage:\n"
        << "  hns_bench --version\n"
        << "  hns_bench --synthetic-bench [--vectors N] [--dim D]\n"
        << "                              [--queries Q] [--warmup W]\n"
        << "                              [--trials T]\n";
}

int run_synthetic_cli(int argc, char** argv) {
    hnswhy::bench::SyntheticConfig config;

    for (int i = 2; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (i + 1 >= argc) {
            std::cerr << "missing value for " << flag << '\n';
            return 2;
        }

        const std::string_view value = argv[++i];
        std::size_t parsed = 0;
        if (!parse_size(value, parsed)) {
            std::cerr << "invalid number for " << flag << ": " << value << '\n';
            return 2;
        }

        if (flag == "--vectors") {
            config.vectors = parsed;
        } else if (flag == "--dim") {
            config.dim = parsed;
        } else if (flag == "--queries") {
            config.queries = parsed;
        } else if (flag == "--warmup") {
            config.warmup = parsed;
        } else if (flag == "--trials") {
            config.trials = parsed;
        } else {
            std::cerr << "unknown option: " << flag << '\n';
            return 2;
        }
    }

    if (config.vectors == 0 || config.dim == 0 || config.queries == 0 ||
        config.trials == 0) {
        std::cerr << "vectors, dim, queries, and trials must be positive\n";
        return 2;
    }

    hnswhy::bench::run_synthetic(config);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << hnswhy::format_version();
        return 0;
    }

    if (argc >= 2 && std::string_view(argv[1]) == "--synthetic-bench") {
        return run_synthetic_cli(argc, argv);
    }

    print_usage(std::cerr);
    return 2;
}

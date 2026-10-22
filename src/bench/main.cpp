#include "hnswhy/build_info.h"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << hnswhy::format_version();
        return 0;
    }

    std::cerr << "usage: hns_bench --version\n";
    return 2;
}

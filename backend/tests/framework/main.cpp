#include "framework/test_framework.h"

#include <cstring>
#include <iostream>
#include <string>

namespace hnswhytest {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(std::string name, std::function<void()> fn) {
    registry().push_back(Case{std::move(name), std::move(fn)});
}

}  // namespace hnswhytest

int main(int argc, char** argv) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        }
    }

    std::size_t run = 0;
    std::size_t failed = 0;
    for (const auto& test : hnswhytest::registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) {
            continue;
        }
        ++run;
        std::cout << "[ RUN  ] " << test.name << "\n";
        try {
            test.fn();
            std::cout << "[  OK  ] " << test.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n         " << e.what() << "\n";
        } catch (...) {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n         unknown exception\n";
        }
    }

    std::cout << "\n" << run - failed << "/" << run << " tests passed"
              << (filter.empty() ? "" : " (filter: " + filter + ")") << "\n";
    if (run == 0) {
        std::cerr << "no tests matched" << (filter.empty() ? "" : " filter '" + filter + "'")
                  << "\n";
        return 2;
    }
    return failed == 0 ? 0 : 1;
}

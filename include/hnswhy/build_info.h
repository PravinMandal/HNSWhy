#pragma once

#include <string>

namespace hnswhy {

struct BuildInfo {
    std::string version;
    std::string build_type;
    std::string compiler;
    std::string cxx_flags;
};

BuildInfo build_info();
std::string format_version();

}  // namespace hnswhy

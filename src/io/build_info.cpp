#include "hnswhy/build_info.h"

#include "hnswhy/build_info_generated.h"
#include "hnswhy/version.h"

#include <sstream>

namespace hnswhy {

BuildInfo build_info() {
    return BuildInfo{
        std::string(version()),
        detail::kBuildType,
        std::string(detail::kCompilerId) + " " + detail::kCompilerVersion,
        detail::kCxxFlags,
    };
}

std::string format_version() {
    const BuildInfo info = build_info();

    std::ostringstream out;
    out << "HNSWhy " << info.version << '\n'
        << "build: " << info.build_type << '\n'
        << "compiler: " << info.compiler << '\n'
        << "flags: " << info.cxx_flags << '\n';
    return out.str();
}

}  // namespace hnswhy

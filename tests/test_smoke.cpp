#include "hnswhy/build_info.h"
#include "hnswhy/version.h"

#include <gtest/gtest.h>

#include <string>

TEST(Version, ReportsProjectVersion) {
    EXPECT_EQ(hnswhy::version(), "0.1.0");
}

TEST(BuildInfo, IncludesBuildTypeAndFlags) {
    const hnswhy::BuildInfo info = hnswhy::build_info();

    EXPECT_FALSE(info.build_type.empty());
    EXPECT_FALSE(info.compiler.empty());
    EXPECT_FALSE(info.cxx_flags.empty());
}

TEST(BuildInfo, FormatVersionContainsFields) {
    const std::string text = hnswhy::format_version();

    EXPECT_NE(text.find("HNSWhy"), std::string::npos);
    EXPECT_NE(text.find("build:"), std::string::npos);
    EXPECT_NE(text.find("compiler:"), std::string::npos);
    EXPECT_NE(text.find("flags:"), std::string::npos);
}

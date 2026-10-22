cmake_minimum_required(VERSION 3.25)

project(HNSWhy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

find_package(GTest REQUIRED)

# Keep the build metadata in one generated header so `hns_bench --version`
# reports the flags that actually compiled the binary.
string(TOUPPER "${CMAKE_BUILD_TYPE}" HNSWHY_BUILD_TYPE_UPPER)
set(HNSWHY_EFFECTIVE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${HNSWHY_BUILD_TYPE_UPPER}}")
string(STRIP "${HNSWHY_EFFECTIVE_CXX_FLAGS}" HNSWHY_EFFECTIVE_CXX_FLAGS)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info_generated.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hnswhy/build_info_generated.h"
    @ONLY
)

add_library(hns_core STATIC
    src/core/version.cpp
)
target_include_directories(hns_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(hns_core PRIVATE
    HNSWHY_VERSION="${PROJECT_VERSION}"
)
target_compile_features(hns_core PUBLIC cxx_std_20)

add_library(hns_io STATIC
    src/io/build_info.cpp
)
target_link_libraries(hns_io PUBLIC hns_core)
target_include_directories(hns_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)

add_executable(hns_bench
    src/bench/main.cpp
)
target_link_libraries(hns_bench PRIVATE hns_io)

add_executable(hns_tests
    tests/test_smoke.cpp
)
target_link_libraries(hns_tests PRIVATE hns_io GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(hns_tests)

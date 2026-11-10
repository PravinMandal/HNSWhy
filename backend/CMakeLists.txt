add_library(vf_engine STATIC
    src/engine/vector_store.cpp
    src/engine/brute_force.cpp
    src/engine/hnsw.cpp
    src/engine/metrics.cpp
)
target_include_directories(vf_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(vf_engine PUBLIC cxx_std_20)
target_compile_options(vf_engine PRIVATE ${VF_WARNINGS})

add_library(vf_io STATIC
    bench/fvecs.cpp
    bench/ground_truth.cpp
)
target_link_libraries(vf_io PUBLIC vf_engine)
target_include_directories(vf_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vf_io PUBLIC cxx_std_20)
target_compile_options(vf_io PRIVATE ${VF_WARNINGS})

add_executable(hns_gt bench/gt_main.cpp)
target_link_libraries(hns_gt PRIVATE vf_io)
target_compile_options(hns_gt PRIVATE ${VF_WARNINGS})

add_executable(hns_bench bench/main.cpp bench/report.cpp)
target_link_libraries(hns_bench PRIVATE vf_io)
target_include_directories(hns_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_compile_options(hns_bench PRIVATE ${VF_WARNINGS})

add_executable(hns_tests
    tests/framework/main.cpp
    tests/unit/test_distance.cpp
    tests/unit/test_vector_store.cpp
    tests/unit/test_brute_force.cpp
    tests/unit/test_fvecs.cpp
    tests/unit/test_hnsw.cpp
    tests/unit/test_prune.cpp
    tests/unit/test_delete.cpp
    tests/unit/test_metrics.cpp
    tests/integration/test_siftsmall.cpp
    tests/integration/test_siftsmall_hnsw.cpp
    tests/integration/test_bench.cpp
    bench/report.cpp
)
target_include_directories(hns_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(hns_tests PRIVATE vf_io)
target_compile_options(hns_tests PRIVATE ${VF_WARNINGS})

add_test(NAME hns_tests COMMAND hns_tests)
set_tests_properties(hns_tests PROPERTIES
    ENVIRONMENT "VF_SIFTSMALL_DIR=${PROJECT_SOURCE_DIR}/data/siftsmall"
)

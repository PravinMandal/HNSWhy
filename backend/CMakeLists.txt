add_library(hnswhy_engine STATIC
    src/engine/vector_store.cpp
    src/engine/brute_force.cpp
    src/engine/hnsw.cpp
    src/engine/metrics.cpp
)
target_include_directories(hnswhy_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(hnswhy_engine PUBLIC cxx_std_20)
target_compile_options(hnswhy_engine PRIVATE ${HNSWHY_WARNINGS})

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL QUIET)

add_library(hnswhy_product STATIC
    src/api/routes.cpp
    src/product/app.cpp
    src/product/chunker.cpp
    src/product/config.cpp
    src/product/document_store.cpp
    src/product/http.cpp
    src/product/ollama_client.cpp
    src/product/providers.cpp
    src/product/rag.cpp
    src/product/text.cpp
    src/product/vector_index.cpp
)
target_link_libraries(hnswhy_product PUBLIC hnswhy_engine SQLite3::SQLite3 Threads::Threads)
target_include_directories(hnswhy_product PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_compile_features(hnswhy_product PUBLIC cxx_std_20)
target_compile_options(hnswhy_product PRIVATE ${HNSWHY_WARNINGS})
if(OpenSSL_FOUND)
    target_compile_definitions(hnswhy_product PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT HNSWHY_HAVE_OPENSSL=1)
    target_link_libraries(hnswhy_product PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

add_library(hnswhy_io STATIC
    bench/fvecs.cpp
    bench/ground_truth.cpp
)
target_link_libraries(hnswhy_io PUBLIC hnswhy_engine)
target_include_directories(hnswhy_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(hnswhy_io PUBLIC cxx_std_20)
target_compile_options(hnswhy_io PRIVATE ${HNSWHY_WARNINGS})

add_executable(hns_gt bench/gt_main.cpp)
target_link_libraries(hns_gt PRIVATE hnswhy_io)
target_compile_options(hns_gt PRIVATE ${HNSWHY_WARNINGS})

add_executable(hnswhy_server src/main.cpp)
target_link_libraries(hnswhy_server PRIVATE hnswhy_product)
target_compile_options(hnswhy_server PRIVATE ${HNSWHY_WARNINGS})

add_executable(hns_bench bench/main.cpp bench/report.cpp)
target_link_libraries(hns_bench PRIVATE hnswhy_io)
target_include_directories(hns_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_compile_options(hns_bench PRIVATE ${HNSWHY_WARNINGS})

add_executable(hns_tests
    tests/framework/main.cpp
    tests/unit/test_framework.cpp
    tests/unit/test_distance.cpp
    tests/unit/test_vector_store.cpp
    tests/unit/test_brute_force.cpp
    tests/unit/test_cli.cpp
    tests/unit/test_fvecs.cpp
    tests/unit/test_hnsw.cpp
    tests/unit/test_prune.cpp
    tests/unit/test_delete.cpp
    tests/unit/test_metrics.cpp
    tests/unit/test_chunker.cpp
    tests/unit/test_config.cpp
    tests/unit/test_document_store.cpp
    tests/unit/test_providers.cpp
    tests/unit/test_vector_index.cpp
    tests/unit/test_rag.cpp
    tests/unit/test_persistence.cpp
    tests/integration/test_api.cpp
    tests/integration/test_sift10k_fixture.cpp
    tests/integration/test_siftsmall.cpp
    tests/integration/test_siftsmall_hnsw.cpp
    tests/integration/test_bench.cpp
    bench/report.cpp
)
target_include_directories(hns_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(hns_tests PRIVATE hnswhy_io hnswhy_product)
target_compile_options(hns_tests PRIVATE ${HNSWHY_WARNINGS})
target_compile_definitions(hns_tests PRIVATE
    HNSWHY_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/sift10k"
    HNSWHY_TEST_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)

add_test(NAME hns_tests COMMAND hns_tests)
# The SIFT-small dataset is gitignored. Only point the tests at it when it is
# actually present; otherwise the tests skip themselves.
if(EXISTS "${PROJECT_SOURCE_DIR}/data/siftsmall")
    set_tests_properties(hns_tests PROPERTIES
        ENVIRONMENT "HNSWHY_SIFTSMALL_DIR=${PROJECT_SOURCE_DIR}/data/siftsmall"
    )
endif()

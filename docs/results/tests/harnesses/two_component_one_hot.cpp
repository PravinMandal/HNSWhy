#include <cstddef>
#include <cstdio>
#include <vector>

#include "engine/hnsw.h"
#include "engine/vector_store.h"

int main() {
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        for (std::size_t m0 : {std::size_t{4}, std::size_t{8}}) {
            for (std::size_t efc : {std::size_t{4}, std::size_t{8}}) {
                constexpr std::size_t n = 64;
                hnswhy::VectorStore store;
                store.reserve(n, n + 4);
                for (std::size_t i = 0; i < n; ++i) {
                    std::vector<float> row(n + 4, 0.0f);
                    row[i] = 1.0f;
                    store.add(row);
                }
                hnswhy::HNSWParams params;
                params.M = 4;
                params.M0 = m0;
                params.efConstruction = efc;
                params.seed = seed;
                hnswhy::HNSW graph(store, params);
                graph.build();
                if (graph.reachableCount(0) == graph.size()) {
                    continue;
                }
                std::size_t wrong = 0;
                for (std::size_t q = 0; q < n; ++q) {
                    const auto r = graph.search(store.get(q), 1, n);
                    if (r.ids.size() != 1 || r.ids[0] != q) {
                        ++wrong;
                        if (wrong == 1) {
                            std::printf("seed=%llu m0=%zu efc=%zu entry=%u reach=%zu q=%zu got=%u\n",
                                        static_cast<unsigned long long>(seed), m0, efc,
                                        graph.entryPoint(), graph.reachableCount(0), q,
                                        r.ids.empty() ? 9999u : r.ids[0]);
                        }
                    }
                }
                if (wrong != 0) {
                    std::printf("  total wrong=%zu\n", wrong);
                    return 0;
                }
            }
        }
    }
    std::printf("no failures\n");
    return 1;
}

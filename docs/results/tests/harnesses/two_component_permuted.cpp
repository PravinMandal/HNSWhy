#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>

#include "engine/hnsw.h"
#include "engine/vector_store.h"

int main() {
    for (std::uint64_t perm_seed = 0; perm_seed < 3000; ++perm_seed) {
        for (std::size_t m0 : {std::size_t{4}, std::size_t{8}, std::size_t{16}}) {
            for (std::size_t efc : {std::size_t{4}, std::size_t{8}, std::size_t{16}}) {
                constexpr std::size_t n = 32;
                std::vector<std::size_t> order(n);
                std::iota(order.begin(), order.end(), 0);
                std::mt19937_64 rng(perm_seed);
                std::shuffle(order.begin(), order.end(), rng);

                hnswhy::VectorStore store;
                store.reserve(n, n + 4);
                for (const std::size_t axis : order) {
                    std::vector<float> row(n + 4, 0.0f);
                    row[axis] = 1.0f;
                    store.add(row);
                }
                hnswhy::HNSWParams params;
                params.M = 4;
                params.M0 = m0;
                params.efConstruction = efc;
                params.seed = perm_seed;
                hnswhy::HNSW graph(store, params);
                graph.build();
                if (graph.reachableCount(0) == graph.size()) {
                    continue;
                }
                std::size_t wrong = 0;
                std::size_t first_q = 0;
                std::size_t first_got = 0;
                for (std::size_t q = 0; q < n; ++q) {
                    const auto r = graph.search(store.get(q), 1, n);
                    if (r.ids.size() != 1 || r.ids[0] != q) {
                        if (wrong == 0) {
                            first_q = q;
                            first_got = r.ids.empty() ? 9999u : r.ids[0];
                        }
                        ++wrong;
                    }
                }
                if (wrong != 0) {
                    std::printf("perm=%llu m0=%zu efc=%zu entry=%u reach=%zu wrong=%zu q=%zu got=%zu\n",
                                static_cast<unsigned long long>(perm_seed), m0, efc,
                                graph.entryPoint(), graph.reachableCount(0), wrong, first_q,
                                first_got);
                    return 0;
                }
            }
        }
    }
    std::printf("no failures\n");
    return 1;
}

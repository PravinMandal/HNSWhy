#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "engine/hnsw.h"

using namespace hnswhy;

int main() {
    int wrong = 0, connected_checks = 0;
    for (std::uint32_t seed = 1; seed <= 120; ++seed) {
        std::mt19937 rng(seed * 104729);
        std::uniform_int_distribution<int> dimd(2, 10);
        std::uniform_int_distribution<int> nd(60, 260);
        std::uniform_int_distribution<int> md(4, 8);
        const std::size_t dim = dimd(rng);
        const std::size_t n0 = nd(rng);
        const std::size_t M = md(rng);
        HNSWParams p;
        p.M = M; p.M0 = M * (1 + seed % 2); p.efConstruction = 4 + seed % 20; p.seed = seed;

        VectorStore store;
        store.reserve(n0 + 200, dim);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> row(dim);
        for (std::size_t i = 0; i < n0; ++i) {
            for (float& v : row) v = u(rng);
            store.add(row);
        }
        HNSW g(store, p);
        g.build();

        std::uniform_int_distribution<int> opd(0, 2);
        for (std::size_t step = 0; step < 300; ++step) {
            const int op = opd(rng);
            if (op == 0 && g.size() > 2) {
                std::uniform_int_distribution<Slot> rd(0, static_cast<Slot>(g.slot_count() - 1));
                g.remove(rd(rng));
            } else if (op == 1 && store.size() < n0 + 200) {
                for (float& v : row) v = u(rng);
                store.add(row);
                g.insert(static_cast<Slot>(store.size() - 1));
            }
            if (g.reachableCount(0) != g.size()) continue;
            // connected: a live self-query must return itself with no fallback
            for (Slot id = 0; id < g.slot_count(); ++id) {
                if (g.isDeleted(id)) continue;
                SearchResult r = g.search(store.get(id), 1, 50);
                ++connected_checks;
                if (r.ids.size() == 1 && r.ids[0] != id && r.stats.exact_fallbacks == 0) {
                    std::fprintf(stderr,
                                 "SILENT WRONG seed=%u step=%zu slot=%u got=%u dist=%.9f "
                                 "fallbacks=%u entry=%u top=%d live=%zu\n",
                                 seed, step, id, r.ids[0], r.distances[0],
                                 r.stats.exact_fallbacks, g.entryPoint(), g.topLevel(), g.size());
                    if (++wrong > 10) return 1;
                }
            }
        }
    }
    std::fprintf(stderr, "checks=%d wrong=%d\n", connected_checks, wrong);
    return 0;
}

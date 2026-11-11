#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "engine/hnsw.h"

using namespace hnswhy;

int main() {
    int bad = 0;
    for (std::uint32_t seed = 1; seed <= 60; ++seed) {
        std::mt19937 rng(seed * 31337);
        const std::size_t dim = 2 + seed % 7;
        HNSWParams p;
        p.M = 4 + seed % 5;
        p.M0 = p.M * (1 + seed % 2);
        p.efConstruction = 1 + seed % 30;
        p.seed = seed;
        VectorStore store;
        store.reserve(600, dim);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        std::vector<float> row(dim);
        for (int i = 0; i < 40; ++i) { for (float& v : row) v = u(rng); store.add(row); }
        HNSW g(store, p);
        g.build();
        std::uniform_int_distribution<int> op(0, 4);
        for (std::size_t step = 0; step < 600; ++step) {
            const int o = op(rng);
            if (o == 0 && g.slot_count() > 1) {
                std::uniform_int_distribution<Slot> rd(0, static_cast<Slot>(g.slot_count() - 1));
                g.remove(rd(rng));
            } else if (o == 1) {
                for (float& v : row) v = u(rng);
                store.add(row);
                g.insert(static_cast<Slot>(store.size() - 1));
            } else if (o == 2 && g.size() > 0) {
                // delete every live node (delete-all cycle)
                for (Slot id = 0; id < g.slot_count(); ++id)
                    if (!g.isDeleted(id)) g.remove(id);
            } else if (o == 3) {
                for (float& v : row) v = u(rng);
                store.add(row);
                g.insert(static_cast<Slot>(store.size() - 1));
                SearchResult r = g.search(row, 1, 50);
                if (r.ids.size() != 1) { std::fprintf(stderr, "count seed=%u step=%zu\n", seed, step); ++bad; }
            }
            try { g.validate(); }
            catch (const std::exception& e) {
                std::fprintf(stderr, "VALIDATE seed=%u step=%zu: %s\n", seed, step, e.what());
                if (++bad > 5) return 1;
            }
        }
    }
    std::fprintf(stderr, "bad=%d\n", bad);
    return 0;
}

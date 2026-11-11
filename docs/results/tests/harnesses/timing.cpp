#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "engine/hnsw.h"

using namespace hnswhy;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    constexpr std::size_t n = 100000;
    constexpr std::size_t dim = 8;
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    VectorStore s;
    s.reserve(n + 10, dim);
    std::vector<float> row(dim);
    for (std::size_t i = 0; i < n; ++i) {
        for (float& v : row) v = u(rng);
        s.add(row);
    }
    HNSWParams p;
    p.M = 16; p.M0 = 32; p.efConstruction = 200; p.seed = 42;
    HNSW g(s, p);
    auto t0 = Clock::now();
    g.build();
    std::fprintf(stderr, "build %.1f ms live=%zu top=%d\n", ms(t0, Clock::now()), g.size(), g.topLevel());

    std::vector<float> q(dim);
    for (float& v : q) v = u(rng);

    // clean search timing
    for (int i = 0; i < 200; ++i) g.search(q, 5, 50);
    auto t1 = Clock::now();
    for (int i = 0; i < 2000; ++i) g.search(q, 5, 50);
    const double clean = ms(t1, Clock::now()) / 2000.0;

    // add one vector (what an upload does), then time the first search
    s.add(row);
    g.insert(static_cast<Slot>(n));
    std::vector<double> dirty;
    for (int round = 0; round < 5; ++round) {
        // alternate: insert then first search
        s.add(row);
        g.insert(static_cast<Slot>(n + 1 + round));
        auto t2 = Clock::now();
        g.search(q, 5, 50);
        dirty.push_back(ms(t2, Clock::now()));
        // follow-up clean search
        g.search(q, 5, 50);
    }
    std::sort(dirty.begin(), dirty.end());
    std::fprintf(stderr, "clean p50 %.4f ms | first-search-after-insert median %.3f ms (%.0fx)\n",
                 clean, dirty[dirty.size() / 2], dirty[dirty.size() / 2] / clean);
    std::fprintf(stderr, "dirty samples:");
    for (double d : dirty) std::fprintf(stderr, " %.3f", d);
    std::fprintf(stderr, "\n");

    // disconnected (all-duplicate) graph: exact-fallback cost per query
    constexpr std::size_t dn = 50000;
    VectorStore dup;
    dup.reserve(dn, 4);
    std::vector<float> zero(4, 0.0f);
    for (std::size_t i = 0; i < dn; ++i) dup.add(zero);
    HNSW dg(dup, p);
    auto t3 = Clock::now();
    dg.build();
    std::fprintf(stderr, "dup build %.1f ms\n", ms(t3, Clock::now()));
    SearchResult probe = dg.search(zero, 5, 50);
    std::fprintf(stderr, "dup fallbacks=%u count=%zu\n", probe.stats.exact_fallbacks, probe.ids.size());
    auto t4 = Clock::now();
    for (int i = 0; i < 50; ++i) dg.search(zero, 5, 50);
    std::fprintf(stderr, "dup query %.3f ms (brute force would be ~%.3f ms at 50k)\n",
                 ms(t4, Clock::now()) / 50.0, 50000 * 4 / 1e6);
    return 0;
}

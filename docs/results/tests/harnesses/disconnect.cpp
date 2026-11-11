#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "engine/brute_force.h"
#include "engine/hnsw.h"

using namespace hnswhy;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    // 1) how often is a product-default build disconnected / does search fall back?
    for (int trial = 0; trial < 5; ++trial) {
        constexpr std::size_t n = 8000, dim = 64;
        std::mt19937 rng(100 + trial);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        VectorStore s;
        s.reserve(n, dim);
        std::vector<float> row(dim);
        for (std::size_t i = 0; i < n; ++i) {
            for (float& v : row) v = u(rng);
            // quantize to 1 decimal: simulates the coarse hash/embedding ties
            for (float& v : row) v = std::round(v * 10.0f) / 10.0f;
            s.add(row);
        }
        HNSWParams p; p.M = 16; p.M0 = 32; p.efConstruction = 200; p.seed = 42;
        HNSW g(s, p);
        g.build();
        SearchResult r = g.search(row, 5, 50);
        std::fprintf(stderr, "trial %d reachable=%zu/%zu fallback=%u dist=%llu\n", trial,
                     g.reachableCount(0), g.size(), r.stats.exact_fallbacks,
                     (unsigned long long)r.stats.distance_computations);
    }

    // 2) fair exact-fallback vs brute force comparison (disconnected duplicate graph)
    constexpr std::size_t dn = 100000;
    VectorStore dup;
    dup.reserve(dn, 4);
    std::vector<float> zero(4, 0.0f);
    for (std::size_t i = 0; i < dn; ++i) dup.add(zero);
    HNSWParams p; p.M = 16; p.M0 = 32; p.efConstruction = 200; p.seed = 42;
    HNSW g(dup, p);
    g.build();
    BruteForce bf(dup);
    auto a = Clock::now();
    for (int i = 0; i < 20; ++i) g.search(zero, 5, 50);
    const double f = ms(a, Clock::now()) / 20.0;
    auto b = Clock::now();
    for (int i = 0; i < 20; ++i) bf.search(zero, 5);
    const double e = ms(b, Clock::now()) / 20.0;
    std::fprintf(stderr, "dup N=%zu exact-fallback %.3f ms | bruteforce %.3f ms | ratio %.2fx\n",
                 dn, f, e, f / e);

    // 3) clean search cost at N=100k dim=128 for reference vs the 4.9ms recompute
    return 0;
}

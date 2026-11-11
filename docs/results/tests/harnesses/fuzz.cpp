#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

#include "engine/brute_force.h"
#include "engine/hnsw.h"
#include "engine/metrics.h"
#include "engine/distance.h"

using namespace hnswhy;

static int failures = 0;

static void fail(const char* what, std::uint32_t seed, std::size_t step) {
    std::fprintf(stderr, "FAIL [%s] seed=%u step=%zu\n", what, seed, step);
    std::fflush(stderr);
    ++failures;
}

static void check_exact(HNSW& g, const VectorStore& s, const std::vector<float>& q,
                        std::size_t k, std::uint32_t seed, std::size_t step) {
    SearchResult r = g.search(q, k, g.slot_count() * 2 + 8);
    std::vector<Candidate> all;
    for (Slot id = 0; id < g.slot_count(); ++id) {
        if (!g.isDeleted(id)) all.push_back(Candidate{squared_l2(q, s.get(id)), id});
    }
    std::sort(all.begin(), all.end(), [](const Candidate& a, const Candidate& b) {
        return a.dist < b.dist || (a.dist == b.dist && a.id < b.id);
    });
    const std::size_t want = std::min(k, all.size());
    if (r.ids.size() != want) {
        std::fprintf(stderr, "count got %zu want %zu\n", r.ids.size(), want);
        fail("count", seed, step);
        return;
    }
    for (std::size_t i = 1; i < r.distances.size(); ++i) {
        if (r.distances[i - 1] > r.distances[i]) { fail("unsorted", seed, step); return; }
    }
    for (std::size_t i = 0; i < r.ids.size(); ++i) {
        if (r.ids[i] >= g.slot_count()) { fail("id-out-of-range", seed, step); return; }
        if (g.isDeleted(r.ids[i])) { fail("deleted-returned", seed, step); return; }
        for (std::size_t j = i + 1; j < r.ids.size(); ++j)
            if (r.ids[i] == r.ids[j]) { fail("duplicate-id", seed, step); return; }
    }
    for (std::size_t i = 0; i < want; ++i) {
        if (std::fabs(r.distances[i] - all[i].dist) >
            1e-5f * std::max(1.0f, std::fabs(all[i].dist))) {
            std::fprintf(stderr, "  i=%zu got %.9g want %.9g (got id %u want %u)\n", i,
                         r.distances[i], all[i].dist, r.ids[i], all[i].id);
            fail("distance-mismatch", seed, step);
            return;
        }
    }
}

int main() {
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        std::fprintf(stderr, "seed %u\n", seed);
        std::mt19937 rng(seed * 7919);
        std::uniform_int_distribution<int> dimd(2, 12);
        std::uniform_int_distribution<int> nd(30, 220);
        std::uniform_int_distribution<int> md(4, 10);
        std::uniform_int_distribution<int> efcd(1, 120);
        std::uniform_int_distribution<int> strat(0, 2);
        const std::size_t dim = dimd(rng);
        const std::size_t n = nd(rng);
        const std::size_t M = md(rng);
        const std::size_t M0 = M * (1 + (seed % 2));
        HNSWParams p;
        p.M = M; p.M0 = M0; p.efConstruction = efcd(rng); p.seed = seed;
        p.prune = strat(rng) == 0 ? PruneStrategy::NearestOnly : PruneStrategy::Heuristic;

        VectorStore shuffled;
        {
            std::mt19937 vrng(seed);
            std::uniform_real_distribution<float> u(-1.0f, 1.0f);
            std::vector<float> row(dim);
            std::vector<std::size_t> order(n);
            for (std::size_t i = 0; i < n; ++i) order[i] = i;
            std::shuffle(order.begin(), order.end(), rng);
            shuffled.reserve(n, dim);
            std::vector<std::vector<float>> rows(n, std::vector<float>(dim));
            for (std::size_t i = 0; i < n; ++i) {
                for (float& v : rows[i]) v = u(vrng);
            }
            for (std::size_t id : order) shuffled.add(rows[id]);
        }
        HNSW gs(shuffled, p);

        for (std::size_t i = 0; i < n; ++i) {
            try { gs.insert(static_cast<Slot>(i)); }
            catch (const std::exception& e) {
                std::fprintf(stderr, "THROW insert seed=%u i=%zu: %s\n", seed, i, e.what());
                ++failures; break;
            }
            try { gs.validate(); }
            catch (const std::exception& e) {
                std::fprintf(stderr, "THROW validate seed=%u i=%zu: %s\n", seed, i, e.what());
                ++failures; break;
            }
            std::vector<float> q(dim);
            std::uniform_real_distribution<float> u(-1.0f, 1.0f);
            for (float& v : q) v = u(rng);
            check_exact(gs, shuffled, q, std::min<std::size_t>(i + 1, 10), seed, i);
            if (failures > 20) return 1;
        }

        std::uniform_int_distribution<int> remd(0, static_cast<int>(n) - 1);
        for (std::size_t step = 0; step < 60; ++step) {
            Slot id = static_cast<Slot>(remd(rng));
            gs.remove(id);
            try { gs.validate(); }
            catch (const std::exception& e) {
                std::fprintf(stderr, "THROW validate-remove seed=%u step=%zu: %s\n", seed, step, e.what());
                ++failures; break;
            }
            std::vector<float> q(dim);
            std::uniform_real_distribution<float> u(-1.0f, 1.0f);
            for (float& v : q) v = u(rng);
            check_exact(gs, shuffled, q, 10, seed, 1000 + step);
            if (failures > 20) return 1;
        }

        VectorStore dup;
        dup.reserve(120, 4);
        std::vector<float> zero(4, 0.0f);
        for (int i = 0; i < 100; ++i) dup.add(zero);
        HNSWParams dp; dp.M = 4; dp.M0 = 8; dp.efConstruction = 20; dp.seed = seed;
        HNSW dg(dup, dp);
        for (Slot i = 0; i < dup.size(); ++i) {
            dg.insert(i);
            try { dg.validate(); }
            catch (const std::exception& e) {
                std::fprintf(stderr, "THROW dup-validate seed=%u i=%u: %s\n", seed, i, e.what());
                ++failures; break;
            }
        }
        SearchResult dr = dg.search(zero, 100, 200);
        if (dr.ids.size() != 100) fail("dup-count", seed, 0);
        for (float d : dr.distances) if (d != 0.0f) fail("dup-distance", seed, 0);
    }
    std::fprintf(stderr, "failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace hnswhy {

using Slot = std::uint32_t;

inline constexpr Slot kInvalidSlot = std::numeric_limits<Slot>::max();

// A scored slot. Comparisons always use (dist, id) so ties are deterministic.
struct Candidate {
    float dist;
    Slot id;
};

struct SearchStats {
    std::uint64_t distance_computations = 0;
    std::uint32_t hops = 0;
    std::uint32_t layer_visits = 0;
    // 1 when the query could not be answered by walking the graph alone:
    // layer 0 was disconnected (the unreachable live slots were scanned
    // exactly and merged) or the graph walk returned fewer than k candidates
    // and the store completed the result. It counts queries, not individual
    // scans, so a query that needed both still reports 1. Tests use this to
    // prove they exercised graph search rather than a fallback.
    std::uint32_t exact_fallbacks = 0;
};

struct SearchResult {
    std::vector<Slot> ids;
    std::vector<float> distances;
    SearchStats stats;
};

}  // namespace hnswhy

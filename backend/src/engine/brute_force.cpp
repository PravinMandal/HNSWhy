#include "engine/brute_force.h"

#include <algorithm>
#include <stdexcept>

#include "engine/distance.h"

namespace hnswhy {
namespace {

// Ascending (distance, slot) order. Used as the comparator for a max-heap so
// the worst candidate sits at the front and can be evicted in O(log k).
bool ascending(const Candidate& a, const Candidate& b) {
    return a.dist < b.dist || (a.dist == b.dist && a.id < b.id);
}

}  // namespace

SearchResult BruteForce::search(std::span<const float> query, std::size_t k) const {
    if (query.size() != store_.dim()) {
        throw std::invalid_argument("BruteForce::search: query dimension mismatch");
    }

    SearchResult result;
    if (k == 0 || store_.size() == 0) {
        return result;
    }

    const std::size_t want = std::min(k, store_.size());
    std::vector<Candidate> heap;
    heap.reserve(want);

    for (Slot id = 0; id < store_.size(); ++id) {
        const Candidate candidate{squared_l2(query, store_.get(id)), id};
        ++result.stats.distance_computations;

        if (heap.size() < want) {
            heap.push_back(candidate);
            std::push_heap(heap.begin(), heap.end(), ascending);
        } else if (ascending(candidate, heap.front())) {
            std::pop_heap(heap.begin(), heap.end(), ascending);
            heap.back() = candidate;
            std::push_heap(heap.begin(), heap.end(), ascending);
        }
    }

    std::sort_heap(heap.begin(), heap.end(), ascending);
    result.ids.reserve(heap.size());
    result.distances.reserve(heap.size());
    for (const Candidate& candidate : heap) {
        result.ids.push_back(candidate.id);
        result.distances.push_back(candidate.dist);
    }
    return result;
}

}  // namespace hnswhy

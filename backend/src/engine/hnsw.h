#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include "engine/types.h"
#include "engine/vector_store.h"

namespace hnswhy {

enum class PruneStrategy { Heuristic, NearestOnly };

struct HNSWParams {
    std::size_t M = 16;
    std::size_t M0 = 32;
    std::size_t efConstruction = 200;
    std::size_t maxLevel = 32;
    std::uint64_t seed = 42;
    PruneStrategy prune = PruneStrategy::Heuristic;
};

// Multi-layer proximity graph over the slots of a VectorStore.
//
// The store owns the vectors; the graph stores only slot IDs. Insertion is
// append-only: insert(id) requires id == slot_count(), so slots never move and
// no mapping table is needed inside the graph.
class HNSW {
public:
    HNSW(const VectorStore& store, HNSWParams params);
    HNSW(const HNSW&) = delete;
    HNSW& operator=(const HNSW&) = delete;

    // Inserts every slot currently in the store. Only valid on an empty graph.
    void build();

    // Throws and leaves the graph unusable (every later operation throws) if an
    // allocation or invariant failure interrupts the insert. The caller must
    // rebuild the graph; this is cheaper and safer than trying to roll back
    // half-written edges. Application::insert_document_locked() already
    // rebuilds from SQLite when VectorIndex::add() throws.
    void insert(Slot id);

    // Marks the node deleted. Edges are kept so the node still routes; queries
    // never return it. Idempotent.
    void remove(Slot id);

    // Returns up to min(k, live) results. Searches are not thread-safe: the
    // graph keeps scratch state and counters. The caller must serialise calls
    // (the product uses one application mutex).
    //
    // HNSW can leave parts of the graph unreachable when many vectors are
    // exactly equidistant (duplicates, one-hot data). The graph tracks layer-0
    // connectivity over live nodes; while it is disconnected, search scans the
    // unreachable live slots exactly and merges them into the graph walk, so no
    // live node is silently omitted without turning every query into a full
    // scan. Connectivity is recomputed lazily after every structural change
    // that can affect it, so a cached "connected" answer cannot outlive an
    // insert, a removed edge, or a deletion. On degenerate data the fallback
    // lasts as long as the live set stays disconnected; it is not a temporary
    // error.
    SearchResult search(std::span<const float> query, std::size_t k, std::size_t ef);

    // Throws std::runtime_error on a structural invariant violation (degree
    // caps, bogus edges, count drift). It does not require every live node to
    // be graph-reachable; see search().
    void validate() const;

    // Number of live nodes reachable from the entry point on the given layer.
    // Tests use this to prove that pruning keeps both clusters connected; it is
    // O(N * M).
    std::size_t reachableCount(int layer) const;

    // Exposed so tests can exercise the prune rule directly.
    std::vector<Slot> selectNeighbours(Slot ref, std::span<const Candidate> candidates,
                                       std::size_t cap) const;

    std::size_t size() const noexcept { return live_count_; }
    std::size_t slot_count() const noexcept { return slot_count_; }
    bool isDeleted(Slot id) const;
    Slot entryPoint() const noexcept { return entry_; }
    int topLevel() const noexcept { return top_level_; }
    std::uint64_t distanceComputations() const noexcept { return distance_computations_; }
    const HNSWParams& params() const noexcept { return params_; }

private:
    using NeighbourList = std::vector<Slot>;

    static bool closer(const Candidate& a, const Candidate& b);
    static bool closer(float a_dist, Slot a_id, float b_dist, Slot b_id);

    std::size_t cap(int layer) const;
    int sampleLevel();

    std::span<const Slot> neighbours(Slot id, int layer) const;
    std::size_t degree(Slot id, int layer) const;
    void addNeighbour(Slot id, int layer, Slot neighbour);
    void setNeighbours(Slot id, int layer, std::span<const Slot> list);

    float distanceTo(std::span<const float> query, Slot id) const;
    float distanceBetween(Slot a, Slot b) const;

    Slot greedySearchLayer(std::span<const float> query, Slot entry, int layer);
    std::vector<Candidate> searchLayer(std::span<const float> query,
                                       std::span<const Slot> entries, std::size_t ef, int layer,
                                       bool liveOnly);
    void connect(Slot id, std::span<const Slot> selected, int layer);
    void recomputeConnectivity();
    void noteEdgeRemoved(Slot target);

    void insertImpl(Slot id);
    void ensureNode(Slot id, int level);
    void ensureCapacity(Slot id);
    void validateParams() const;
    void requireUsable() const;

    const VectorStore& store_;
    HNSWParams params_;
    std::mt19937_64 rng_;

    // Layer 0: fixed-capacity flat adjacency.
    std::vector<Slot> links0_;
    std::vector<std::uint16_t> degree0_;
    std::size_t link_capacity_ = 0;

    // Upper layers: upper_[slot][layer - 1]. Only ~1/M of nodes have any.
    std::vector<std::vector<NeighbourList>> upper_;

    std::vector<std::uint8_t> level_;
    std::vector<std::uint8_t> deleted_;
    std::vector<std::uint32_t> level_count_;
    std::vector<std::uint32_t> in_degree0_;
    bool layer0_connected_ = true;
    bool layer0_dirty_ = false;
    // Live slots the layer-0 walk from entry_ cannot reach. Only meaningful
    // while layer0_connected_ is false; search scans exactly these slots and
    // merges them into the graph walk instead of scanning every live vector.
    std::vector<Slot> unreachable_live_;

    std::size_t slot_count_ = 0;
    std::size_t live_count_ = 0;
    Slot entry_ = kInvalidSlot;
    int top_level_ = -1;
    // Set when insert() fails after it started mutating the graph. The graph
    // is not safe to query after that, so every operation fails loudly until
    // the owner replaces it.
    bool poisoned_ = false;

    mutable std::vector<std::uint32_t> visit_tag_;
    mutable std::uint32_t visit_generation_ = 0;
    mutable std::uint64_t distance_computations_ = 0;
    mutable std::uint64_t hops_ = 0;
};

}  // namespace hnswhy

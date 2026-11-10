#include "engine/hnsw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>

#include "engine/distance.h"

namespace hnswhy {
namespace {

struct FartherFirst {
    bool operator()(const Candidate& a, const Candidate& b) const {
        return a.dist > b.dist || (a.dist == b.dist && a.id > b.id);
    }
};

struct CloserFirst {
    bool operator()(const Candidate& a, const Candidate& b) const {
        return a.dist < b.dist || (a.dist == b.dist && a.id < b.id);
    }
};

}  // namespace

HNSW::HNSW(const VectorStore& store, HNSWParams params)
    : store_(store), params_(params), rng_(params.seed) {
    validateParams();
}

void HNSW::validateParams() const {
    if (params_.M < 4) {
        throw std::invalid_argument("HNSW: M must be at least 4");
    }
    if (params_.M0 < params_.M) {
        throw std::invalid_argument("HNSW: M0 must be >= M");
    }
    if (params_.M0 > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("HNSW: M0 must fit in uint16_t");
    }
    if (params_.efConstruction < 1) {
        throw std::invalid_argument("HNSW: efConstruction must be positive");
    }
    if (params_.maxLevel > 255) {
        throw std::invalid_argument("HNSW: maxLevel must fit in uint8_t");
    }
}

bool HNSW::closer(const Candidate& a, const Candidate& b) {
    return closer(a.dist, a.id, b.dist, b.id);
}

bool HNSW::closer(float a_dist, Slot a_id, float b_dist, Slot b_id) {
    return a_dist < b_dist || (a_dist == b_dist && a_id < b_id);
}

std::size_t HNSW::cap(int layer) const {
    return layer == 0 ? params_.M0 : params_.M;
}

void HNSW::ensureCapacity(Slot id) {
    if (id < link_capacity_) {
        return;
    }
    const std::size_t new_capacity =
        std::max<std::size_t>({static_cast<std::size_t>(id) + 1, std::size_t{16},
                               link_capacity_ * 2});
    links0_.resize(new_capacity * params_.M0, kInvalidSlot);
    degree0_.resize(new_capacity, 0);
    in_degree0_.resize(new_capacity, 0);
    visit_tag_.resize(new_capacity, 0);
    link_capacity_ = new_capacity;
}

void HNSW::ensureNode(Slot id, int level) {
    if (id != slot_count_) {
        throw std::logic_error("HNSW::insert: insertion must be append-only");
    }
    if (id >= store_.size()) {
        throw std::out_of_range("HNSW::insert: slot not in store");
    }

    ensureCapacity(id);
    level_.resize(static_cast<std::size_t>(id) + 1);
    deleted_.resize(static_cast<std::size_t>(id) + 1);
    if (level_count_.size() < static_cast<std::size_t>(level) + 1) {
        level_count_.resize(static_cast<std::size_t>(level) + 1, 0);
    }
    if (upper_.size() <= id) {
        upper_.resize(static_cast<std::size_t>(id) + 1);
    }

    level_[id] = static_cast<std::uint8_t>(level);
    deleted_[id] = 0;
    degree0_[id] = 0;
    in_degree0_[id] = 0;
    upper_[id].assign(static_cast<std::size_t>(level), NeighbourList{});
}

int HNSW::sampleLevel() {
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const double u = std::max(uniform(rng_), std::numeric_limits<double>::min());
    const double level = -std::log(u) / std::log(static_cast<double>(params_.M));
    const auto sampled = static_cast<std::size_t>(std::max(0.0, level));
    return static_cast<int>(std::min(sampled, params_.maxLevel));
}

std::span<const Slot> HNSW::neighbours(Slot id, int layer) const {
    if (layer == 0) {
        return std::span<const Slot>(links0_.data() + static_cast<std::size_t>(id) * params_.M0,
                                     degree0_[id]);
    }
    if (layer > level_[id]) {
        return {};
    }
    return upper_[id][static_cast<std::size_t>(layer - 1)];
}

std::size_t HNSW::degree(Slot id, int layer) const {
    return neighbours(id, layer).size();
}

void HNSW::addNeighbour(Slot id, int layer, Slot neighbour) {
    if (layer == 0) {
        if (degree0_[id] >= params_.M0) {
            throw std::logic_error("HNSW: layer 0 adjacency overflow");
        }
        const std::size_t base = static_cast<std::size_t>(id) * params_.M0;
        links0_[base + degree0_[id]] = neighbour;
        ++degree0_[id];
        ++in_degree0_[neighbour];
    } else {
        auto& list = upper_[id][static_cast<std::size_t>(layer - 1)];
        if (list.size() >= params_.M) {
            throw std::logic_error("HNSW: upper adjacency overflow");
        }
        list.push_back(neighbour);
    }
}

void HNSW::setNeighbours(Slot id, int layer, std::span<const Slot> list) {
    if (layer == 0) {
        const std::span<const Slot> current = neighbours(id, 0);
        const std::vector<Slot> previous(current.begin(), current.end());
        for (const Slot old : previous) {
            if (std::find(list.begin(), list.end(), old) == list.end()) {
                noteEdgeRemoved(old);
            }
        }
        const std::size_t base = static_cast<std::size_t>(id) * params_.M0;
        std::copy(list.begin(), list.end(), links0_.begin() + static_cast<std::ptrdiff_t>(base));
        degree0_[id] = static_cast<std::uint16_t>(list.size());
        for (const Slot neighbour : list) {
            if (std::find(previous.begin(), previous.end(), neighbour) == previous.end()) {
                ++in_degree0_[neighbour];
            }
        }
    } else {
        auto& target = upper_[id][static_cast<std::size_t>(layer - 1)];
        target.assign(list.begin(), list.end());
    }
}

float HNSW::distanceTo(std::span<const float> query, Slot id) const {
    ++distance_computations_;
    return squared_l2(query, store_.get(id));
}

float HNSW::distanceBetween(Slot a, Slot b) const {
    ++distance_computations_;
    return squared_l2(store_.get(a), store_.get(b));
}

Slot HNSW::greedySearchLayer(std::span<const float> query, Slot entry, int layer) {
    Slot current = entry;
    float current_dist = distanceTo(query, current);
    bool improved = true;
    while (improved) {
        improved = false;
        ++hops_;
        for (const Slot neighbour : neighbours(current, layer)) {
            const float dist = distanceTo(query, neighbour);
            if (closer(dist, neighbour, current_dist, current)) {
                current = neighbour;
                current_dist = dist;
                improved = true;
            }
        }
    }
    return current;
}

std::vector<Candidate> HNSW::searchLayer(std::span<const float> query,
                                         std::span<const Slot> entries, std::size_t ef, int layer,
                                         bool liveOnly) {
    if (ef == 0) {
        ef = 1;
    }

    if (++visit_generation_ == 0) {
        std::fill(visit_tag_.begin(), visit_tag_.end(), 0);
        visit_generation_ = 1;
    }
    const std::uint32_t generation = visit_generation_;
    auto firstVisit = [&](Slot id) {
        if (visit_tag_[id] == generation) {
            return false;
        }
        visit_tag_[id] = generation;
        return true;
    };

    std::priority_queue<Candidate, std::vector<Candidate>, FartherFirst> candidates;
    std::priority_queue<Candidate, std::vector<Candidate>, CloserFirst> results;

    for (const Slot entry : entries) {
        if (!firstVisit(entry)) {
            continue;
        }
        const Candidate candidate{distanceTo(query, entry), entry};
        candidates.push(candidate);
        if (!liveOnly || !deleted_[entry]) {
            results.push(candidate);
            if (results.size() > ef) {
                results.pop();
            }
        }
    }

    while (!candidates.empty()) {
        const Candidate current = candidates.top();
        if (results.size() >= ef && closer(results.top().dist, results.top().id, current.dist,
                                          current.id)) {
            break;
        }
        candidates.pop();
        ++hops_;

        for (const Slot neighbour : neighbours(current.id, layer)) {
            if (!firstVisit(neighbour)) {
                continue;
            }
            const Candidate candidate{distanceTo(query, neighbour), neighbour};
            if (results.size() < ef || closer(candidate, results.top())) {
                candidates.push(candidate);
                if (!liveOnly || !deleted_[neighbour]) {
                    results.push(candidate);
                    if (results.size() > ef) {
                        results.pop();
                    }
                }
            }
        }
    }

    std::vector<Candidate> ordered;
    ordered.reserve(results.size());
    while (!results.empty()) {
        ordered.push_back(results.top());
        results.pop();
    }
    std::reverse(ordered.begin(), ordered.end());
    return ordered;
}

std::vector<Slot> HNSW::selectNeighbours(Slot ref, std::span<const Candidate> candidates,
                                         std::size_t cap) const {
    std::vector<Candidate> sorted(candidates.begin(), candidates.end());
    std::sort(sorted.begin(), sorted.end(), [](const Candidate& a, const Candidate& b) {
        if (a.dist != b.dist) {
            return a.dist < b.dist;
        }
        // Exact duplicates are the one tie where the older copy must not win:
        // every full neighbour would otherwise keep the oldest copies and
        // reject each new duplicate, leaving it unreachable. Prefer the newest
        // zero-distance copy so the chain extends forward.
        if (a.dist == 0.0f) {
            return a.id > b.id;
        }
        return a.id < b.id;
    });

    std::vector<Slot> selected;
    selected.reserve(cap);
    if (cap == 0) {
        return selected;
    }
    for (const Candidate& candidate : sorted) {
        if (candidate.id == ref ||
            std::find(selected.begin(), selected.end(), candidate.id) != selected.end()) {
            continue;
        }

        if (params_.prune == PruneStrategy::NearestOnly || selected.empty()) {
            selected.push_back(candidate.id);
        } else {
            bool diverse = true;
            for (const Slot chosen : selected) {
                const float to_chosen = distanceBetween(candidate.id, chosen);
                // Reject strictly redundant candidates. Zero distance means an
                // exact duplicate: keep it so every copy stays reachable (the
                // capacity cap still bounds how many are linked).
                if (to_chosen > 0.0f && to_chosen < candidate.dist) {
                    diverse = false;
                    break;
                }
            }
            if (diverse) {
                selected.push_back(candidate.id);
            }
        }

        if (selected.size() == cap) {
            break;
        }
    }
    return selected;
}

void HNSW::connect(Slot id, std::span<const Slot> selected, int layer) {
    for (const Slot neighbour : selected) {
        addNeighbour(id, layer, neighbour);

        if (degree(neighbour, layer) < cap(layer)) {
            addNeighbour(neighbour, layer, id);
            continue;
        }

        // The neighbour is full. Add the new edge to the candidate pool and
        // prune the whole pool, so the new node competes with the old edges
        // instead of being appended after an unconditional eviction.
        std::vector<Candidate> pool;
        pool.reserve(degree(neighbour, layer) + 1);
        for (const Slot other : neighbours(neighbour, layer)) {
            pool.push_back(Candidate{distanceBetween(neighbour, other), other});
        }
        pool.push_back(Candidate{distanceBetween(neighbour, id), id});
        const std::vector<Slot> kept = selectNeighbours(neighbour, pool, cap(layer));
        setNeighbours(neighbour, layer, kept);
    }
}

void HNSW::noteEdgeRemoved(Slot target) {
    if (in_degree0_[target] > 0) {
        --in_degree0_[target];
    }
    // Removing any layer-0 edge can split the graph even when the target keeps
    // other incoming edges. Mark the graph dirty; search recomputes live
    // reachability before using the approximate path again.
    layer0_connected_ = false;
    layer0_dirty_ = true;
}

void HNSW::recomputeConnectivity() {
    if (slot_count_ == 0 || live_count_ == 0) {
        layer0_connected_ = true;
        layer0_dirty_ = false;
        return;
    }
    std::vector<std::uint8_t> seen(slot_count_, 0);
    std::vector<Slot> stack;
    stack.push_back(entry_);
    seen[entry_] = 1;
    std::size_t reached_live = 0;
    while (!stack.empty()) {
        const Slot current = stack.back();
        stack.pop_back();
        if (!deleted_[current]) {
            ++reached_live;
        }
        for (const Slot neighbour : neighbours(current, 0)) {
            if (!seen[neighbour]) {
                seen[neighbour] = 1;
                stack.push_back(neighbour);
            }
        }
    }
    layer0_connected_ = reached_live == live_count_;
    layer0_dirty_ = false;
}

void HNSW::insert(Slot id) {
    if (id >= store_.size()) {
        throw std::out_of_range("HNSW::insert: slot not in store");
    }

    const int level = sampleLevel();
    ensureNode(id, level);

    if (slot_count_ == 0) {
        entry_ = id;
        top_level_ = level;
        slot_count_ = 1;
        ++live_count_;
        for (int layer = 0; layer <= level; ++layer) {
            ++level_count_[static_cast<std::size_t>(layer)];
        }
        return;
    }

    const std::span<const float> query = store_.get(id);
    Slot entry = entry_;

    for (int layer = top_level_; layer > level; --layer) {
        entry = greedySearchLayer(query, entry, layer);
    }

    const int start_layer = std::min(level, top_level_);
    for (int layer = start_layer; layer >= 0; --layer) {
        const std::size_t beam = std::max(params_.efConstruction, cap(layer));
        const std::array<Slot, 1> entry_array{entry};
        const std::vector<Candidate> candidates =
            searchLayer(query, entry_array, beam, layer, false);
        if (candidates.empty()) {
            break;
        }

        const std::vector<Slot> selected = selectNeighbours(id, candidates, cap(layer));
        connect(id, selected, layer);
        entry = candidates.front().id;
    }

    ++slot_count_;
    ++live_count_;
    for (int layer = 0; layer <= level; ++layer) {
        ++level_count_[static_cast<std::size_t>(layer)];
    }
    if (level > top_level_) {
        entry_ = id;
        top_level_ = level;
    }

    // A successful insert only proves the new node can be reached from the
    // layer-0 beam entry. Upper-layer descent may have chosen that entry from a
    // different part of the graph than entry_, so this does not prove every
    // live node is reachable from entry_ along layer-0 edges. Mark the cached
    // reachability stale; the next search recomputes it once. A wrong
    // "connected" answer would skip the exact fallback and could return a
    // truncated or incorrect top-k.
    layer0_dirty_ = true;
}

void HNSW::build() {
    if (slot_count_ != 0) {
        throw std::logic_error("HNSW::build: graph is not empty");
    }
    for (Slot id = 0; id < store_.size(); ++id) {
        insert(id);
    }
    recomputeConnectivity();
}

void HNSW::remove(Slot id) {
    if (id >= slot_count_) {
        throw std::out_of_range("HNSW::remove: slot out of range");
    }
    if (deleted_[id] == 0) {
        deleted_[id] = 1;
        --live_count_;
        // Removal tombstones the node but keeps its edges, so the graph can
        // only become more reachable. Re-check before the next search instead
        // of leaving a disconnected flag stuck on forever.
        layer0_dirty_ = true;
    }
}

bool HNSW::isDeleted(Slot id) const {
    if (id >= slot_count_) {
        throw std::out_of_range("HNSW::isDeleted: slot out of range");
    }
    return deleted_[id] != 0;
}

SearchResult HNSW::search(std::span<const float> query, std::size_t k, std::size_t ef) {
    SearchResult result;
    if (k == 0 || slot_count_ == 0) {
        return result;
    }
    if (query.size() != store_.dim()) {
        throw std::invalid_argument("HNSW::search: query dimension mismatch");
    }
    if (layer0_dirty_) {
        recomputeConnectivity();
    }

    const std::uint64_t distances_before = distance_computations_;
    const std::uint64_t hops_before = hops_;

    // A disconnected layer 0 cannot be trusted for top-k: a graph component
    // reachable from the entry point may not contain the query's true nearest
    // neighbours. Fall back to an exact scan for those degenerate graphs so
    // results stay exact and prefix-consistent across k.
    if (!layer0_connected_) {
        ++result.stats.exact_fallbacks;
        std::vector<Candidate> all;
        all.reserve(live_count_);
        for (Slot id = 0; id < slot_count_; ++id) {
            if (deleted_[id] == 0) {
                all.push_back(Candidate{distanceTo(query, id), id});
            }
        }
        std::sort(all.begin(), all.end(),
                  [](const Candidate& a, const Candidate& b) { return closer(a, b); });
        const std::size_t take = std::min(k, all.size());
        result.ids.reserve(take);
        result.distances.reserve(take);
        for (std::size_t i = 0; i < take; ++i) {
            result.ids.push_back(all[i].id);
            result.distances.push_back(all[i].dist);
        }
        result.stats.distance_computations = distance_computations_ - distances_before;
        result.stats.hops = static_cast<std::uint32_t>(hops_ - hops_before);
        return result;
    }

    Slot entry = entry_;
    for (int layer = top_level_; layer > 0; --layer) {
        entry = greedySearchLayer(query, entry, layer);
        ++result.stats.layer_visits;
    }

    const std::size_t ef_effective = std::min(std::max(ef, k), slot_count_);
    const std::array<Slot, 1> entry_array{entry};
    std::vector<Candidate> candidates =
        searchLayer(query, entry_array, ef_effective, 0, true);
    ++result.stats.layer_visits;

    // Tie-heavy graphs (duplicates, one-hot vectors) can be disconnected: a
    // node inserted after the layer-0 degree cap may have no incoming edge.
    // Completing a short result from the store keeps the public contract
    // (min(k, live) results) without turning normal searches into a scan.
    const std::size_t target = std::min(k, live_count_);
    if (candidates.size() < target) {
        ++result.stats.exact_fallbacks;
        std::vector<std::uint8_t> seen(slot_count_, 0);
        for (const Candidate& candidate : candidates) {
            seen[candidate.id] = 1;
        }
        candidates.reserve(target);
        for (Slot id = 0; id < slot_count_; ++id) {
            if (deleted_[id] != 0 || seen[id] != 0) {
                continue;
            }
            candidates.push_back(Candidate{distanceTo(query, id), id});
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return closer(a, b); });
    }

    const std::size_t take = std::min(k, candidates.size());
    result.ids.reserve(take);
    result.distances.reserve(take);
    for (std::size_t i = 0; i < take; ++i) {
        result.ids.push_back(candidates[i].id);
        result.distances.push_back(candidates[i].dist);
    }

    result.stats.distance_computations = distance_computations_ - distances_before;
    result.stats.hops = static_cast<std::uint32_t>(hops_ - hops_before);
    return result;
}

std::size_t HNSW::reachableCount(int layer) const {
    if (slot_count_ == 0 || layer < 0 || layer > top_level_) {
        return 0;
    }
    std::vector<std::uint8_t> seen(slot_count_, 0);
    std::vector<Slot> stack;
    stack.push_back(entry_);
    seen[entry_] = 1;
    std::size_t reached = 0;
    while (!stack.empty()) {
        const Slot current = stack.back();
        stack.pop_back();
        if (level_[current] < layer) {
            continue;
        }
        if (deleted_[current] == 0) {
            ++reached;
        }
        for (const Slot neighbour : neighbours(current, layer)) {
            if (!seen[neighbour]) {
                seen[neighbour] = 1;
                stack.push_back(neighbour);
            }
        }
    }
    return reached;
}

void HNSW::validate() const {
    if (slot_count_ == 0) {
        if (entry_ != kInvalidSlot || top_level_ != -1) {
            throw std::runtime_error("HNSW::validate: empty graph has an entry point");
        }
        return;
    }

    if (entry_ == kInvalidSlot || entry_ >= slot_count_) {
        throw std::runtime_error("HNSW::validate: invalid entry point");
    }
    if (top_level_ < 0 || level_[entry_] != top_level_) {
        throw std::runtime_error("HNSW::validate: entry level does not match top level");
    }

    std::size_t live = 0;
    std::vector<std::uint32_t> expected_level_count(static_cast<std::size_t>(top_level_) + 1, 0);
    std::vector<std::uint32_t> expected_in_degree(slot_count_, 0);
    for (Slot id = 0; id < slot_count_; ++id) {
        if (level_[id] > top_level_) {
            throw std::runtime_error("HNSW::validate: node level above top level");
        }
        if (!deleted_[id]) {
            ++live;
        }
        for (int layer = 0; layer <= level_[id]; ++layer) {
            ++expected_level_count[static_cast<std::size_t>(layer)];
        }
        if (degree0_[id] > params_.M0) {
            throw std::runtime_error("HNSW::validate: layer 0 degree above M0");
        }
        if (upper_[id].size() != level_[id]) {
            throw std::runtime_error("HNSW::validate: upper layer count mismatch");
        }

        const std::span<const Slot> layer0 = neighbours(id, 0);
        for (const Slot neighbour : layer0) {
            if (neighbour >= slot_count_ || neighbour == id) {
                throw std::runtime_error("HNSW::validate: invalid layer 0 edge");
            }
            if (std::count(layer0.begin(), layer0.end(), neighbour) != 1) {
                throw std::runtime_error("HNSW::validate: duplicate layer 0 edge from " +
                                         std::to_string(id) + " to " +
                                         std::to_string(neighbour) + " (degree " +
                                         std::to_string(degree0_[id]) + ")");
            }
            ++expected_in_degree[neighbour];
        }

        for (int layer = 1; layer <= level_[id]; ++layer) {
            const std::span<const Slot> list = neighbours(id, layer);
            if (list.size() > params_.M) {
                throw std::runtime_error("HNSW::validate: upper degree above M");
            }
            for (const Slot neighbour : list) {
                if (neighbour >= slot_count_ || neighbour == id) {
                    throw std::runtime_error("HNSW::validate: invalid upper edge");
                }
                if (level_[neighbour] < layer) {
                    throw std::runtime_error("HNSW::validate: edge to node below layer");
                }
                if (std::count(list.begin(), list.end(), neighbour) != 1) {
                    throw std::runtime_error("HNSW::validate: duplicate upper edge");
                }
            }
        }
    }

    if (level_count_.size() != expected_level_count.size()) {
        throw std::runtime_error("HNSW::validate: level count vector size mismatch");
    }
    for (std::size_t layer = 0; layer < level_count_.size(); ++layer) {
        if (level_count_[layer] != expected_level_count[layer]) {
            throw std::runtime_error("HNSW::validate: level count mismatch at layer " +
                                     std::to_string(layer));
        }
    }
    for (Slot id = 0; id < slot_count_; ++id) {
        if (in_degree0_[id] != expected_in_degree[id]) {
            throw std::runtime_error("HNSW::validate: layer 0 in-degree mismatch at slot " +
                                     std::to_string(id));
        }
    }
    if (live != live_count_) {
        throw std::runtime_error("HNSW::validate: live count mismatch");
    }
}

}  // namespace hnswhy

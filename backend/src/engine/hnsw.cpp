#include "engine/hnsw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>

#include "engine/distance.h"

namespace vf {
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
    if (upper_.size() <= id) {
        upper_.resize(static_cast<std::size_t>(id) + 1);
    }

    level_[id] = static_cast<std::uint8_t>(level);
    deleted_[id] = 0;
    degree0_[id] = 0;
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
        const std::size_t base = static_cast<std::size_t>(id) * params_.M0;
        std::copy(list.begin(), list.end(), links0_.begin() + static_cast<std::ptrdiff_t>(base));
        degree0_[id] = static_cast<std::uint16_t>(list.size());
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
    std::sort(sorted.begin(), sorted.end(),
              [](const Candidate& a, const Candidate& b) { return closer(a, b); });

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

        // Make room before the back-edge: layer 0 uses a fixed-width row, so a
        // write at capacity would spill into the next node's row. The target is
        // cap - 1 because the new edge is added right after.
        if (degree(neighbour, layer) >= cap(layer)) {
            prune(neighbour, layer, cap(layer) - 1);
        }
        addNeighbour(neighbour, layer, id);
    }
}

void HNSW::prune(Slot id, int layer, std::size_t limit) {
    if (degree(id, layer) <= limit) {
        return;
    }

    const std::span<const Slot> current = neighbours(id, layer);
    std::vector<Candidate> candidates;
    candidates.reserve(current.size());
    for (const Slot neighbour : current) {
        candidates.push_back(Candidate{distanceBetween(id, neighbour), neighbour});
    }

    const std::vector<Slot> selected = selectNeighbours(id, candidates, limit);
    setNeighbours(id, layer, selected);
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
    if (level > top_level_) {
        entry_ = id;
        top_level_ = level;
    }
}

void HNSW::build() {
    if (slot_count_ != 0) {
        throw std::logic_error("HNSW::build: graph is not empty");
    }
    for (Slot id = 0; id < store_.size(); ++id) {
        insert(id);
    }
}

void HNSW::remove(Slot id) {
    if (id >= slot_count_) {
        throw std::out_of_range("HNSW::remove: slot out of range");
    }
    if (deleted_[id] == 0) {
        deleted_[id] = 1;
        --live_count_;
    }
}

bool HNSW::isDeleted(Slot id) const {
    if (id >= slot_count_) {
        throw std::out_of_range("HNSW::isDeleted: slot out of range");
    }
    return deleted_[id] != 0;
}

SearchResult HNSW::search(std::span<const float> query, std::size_t k, std::size_t ef) {
    if (query.size() != store_.dim()) {
        throw std::invalid_argument("HNSW::search: query dimension mismatch");
    }

    SearchResult result;
    if (k == 0 || slot_count_ == 0) {
        return result;
    }

    const std::uint64_t distances_before = distance_computations_;
    const std::uint64_t hops_before = hops_;

    Slot entry = entry_;
    for (int layer = top_level_; layer > 0; --layer) {
        entry = greedySearchLayer(query, entry, layer);
        ++result.stats.layer_visits;
    }

    const std::size_t ef_effective = std::min(std::max(ef, k), slot_count_);
    const std::array<Slot, 1> entry_array{entry};
    const std::vector<Candidate> candidates =
        searchLayer(query, entry_array, ef_effective, 0, true);
    ++result.stats.layer_visits;

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
    for (Slot id = 0; id < slot_count_; ++id) {
        if (level_[id] > top_level_) {
            throw std::runtime_error("HNSW::validate: node level above top level");
        }
        if (!deleted_[id]) {
            ++live;
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

    if (live != live_count_) {
        throw std::runtime_error("HNSW::validate: live count mismatch");
    }
}

}  // namespace vf

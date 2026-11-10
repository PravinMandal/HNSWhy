#include "product/vector_index.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include "product/errors.h"
#include "product/text.h"

namespace hnswhy {
namespace {

std::size_t common_dimension(const std::vector<IndexedVector>& vectors) {
    std::size_t dim = 0;
    for (const IndexedVector& record : vectors) {
        if (record.embedding.empty()) {
            throw validation_error("an embedding is empty");
        }
        if (dim == 0) {
            dim = record.embedding.size();
        } else if (record.embedding.size() != dim) {
            throw validation_error("embeddings have inconsistent dimensions");
        }
        if (!finite_embedding(record.embedding)) {
            throw validation_error("an embedding contains a non-finite value");
        }
    }
    return dim;
}

}  // namespace

VectorIndex::VectorIndex(HNSWParams params) : params_(params) {}

void VectorIndex::rebuild(const std::vector<IndexedVector>& vectors) {
    ++rebuild_count_;
    if (vectors.empty()) {
        hnsw_.reset();
        store_.reset();
        slot_to_chunk_.clear();
        chunk_to_slot_.clear();
        return;
    }

    const std::size_t dim = common_dimension(vectors);
    auto store = std::make_unique<VectorStore>();
    store->reserve(vectors.size(), dim);
    std::vector<Id> slot_to_chunk;
    slot_to_chunk.reserve(vectors.size());
    std::unordered_map<Id, Slot> chunk_to_slot;
    chunk_to_slot.reserve(vectors.size());
    for (const IndexedVector& record : vectors) {
        const Slot slot = store->add(record.embedding);
        if (!chunk_to_slot.emplace(record.chunk_id, slot).second) {
            throw validation_error("duplicate chunk id in the vector index");
        }
        slot_to_chunk.push_back(record.chunk_id);
    }

    auto graph = std::make_unique<HNSW>(*store, params_);
    graph->build();

    store_ = std::move(store);
    hnsw_ = std::move(graph);
    slot_to_chunk_ = std::move(slot_to_chunk);
    chunk_to_slot_ = std::move(chunk_to_slot);
}

void VectorIndex::add(const std::vector<IndexedVector>& vectors) {
    if (vectors.empty()) {
        return;
    }
    const std::size_t dim = common_dimension(vectors);
    if (store_ != nullptr && store_->dim() != 0 && store_->dim() != dim) {
        throw validation_error("embedding dimension does not match the active index");
    }

    // Validate the whole batch before touching the index. A duplicate halfway
    // through must not leave the earlier records in the store and graph.
    std::unordered_set<Id> incoming;
    incoming.reserve(vectors.size());
    for (const IndexedVector& record : vectors) {
        if (chunk_to_slot_.count(record.chunk_id) != 0 || !incoming.insert(record.chunk_id).second) {
            throw validation_error("chunk id already exists in the vector index");
        }
    }

    if (store_ == nullptr) {
        // Construct both objects locally and publish them only after HNSW
        // accepts the parameters. Otherwise a bad M/M0 pair leaves store_ set
        // and hnsw_ null, and the next upload dereferences a null graph.
        auto store = std::make_unique<VectorStore>();
        store->reserve(vectors.size(), dim);
        auto graph = std::make_unique<HNSW>(*store, params_);
        store_ = std::move(store);
        hnsw_ = std::move(graph);
    }

    for (const IndexedVector& record : vectors) {
        const Slot slot = store_->add(record.embedding);
        slot_to_chunk_.push_back(record.chunk_id);
        chunk_to_slot_.emplace(record.chunk_id, slot);
        hnsw_->insert(slot);
    }
}

void VectorIndex::remove(const std::vector<Id>& chunk_ids) {
    if (hnsw_ == nullptr) {
        return;
    }
    for (const Id chunk_id : chunk_ids) {
        const auto it = chunk_to_slot_.find(chunk_id);
        if (it == chunk_to_slot_.end()) {
            continue;
        }
        hnsw_->remove(it->second);
    }
}

std::vector<IndexHit> VectorIndex::search(std::span<const float> query, std::size_t k,
                                          std::size_t ef) {
    if (hnsw_ == nullptr || k == 0) {
        return {};
    }
    if (store_ == nullptr || query.size() != store_->dim()) {
        throw validation_error("query embedding dimension does not match the index");
    }
    const SearchResult result = hnsw_->search(query, k, ef);
    std::vector<IndexHit> hits;
    hits.reserve(result.ids.size());
    for (std::size_t i = 0; i < result.ids.size(); ++i) {
        const Slot slot = result.ids[i];
        if (slot >= slot_to_chunk_.size()) {
            throw std::runtime_error("vector index returned an unmapped slot");
        }
        hits.push_back(IndexHit{slot_to_chunk_[slot], result.distances[i]});
    }
    return hits;
}

std::optional<Slot> VectorIndex::slot_for_chunk(Id chunk_id) const {
    const auto it = chunk_to_slot_.find(chunk_id);
    if (it == chunk_to_slot_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::span<const float>> VectorIndex::vector_for_chunk(Id chunk_id) const {
    const auto slot = slot_for_chunk(chunk_id);
    if (!slot.has_value() || store_ == nullptr) {
        return std::nullopt;
    }
    return store_->get(*slot);
}

std::size_t VectorIndex::dim() const noexcept {
    return store_ == nullptr ? 0 : store_->dim();
}

std::size_t VectorIndex::live() const noexcept {
    return hnsw_ == nullptr ? 0 : hnsw_->size();
}

std::size_t VectorIndex::slot_count() const noexcept {
    return hnsw_ == nullptr ? 0 : hnsw_->slot_count();
}

std::size_t VectorIndex::rebuild_count() const noexcept {
    return rebuild_count_;
}

void VectorIndex::validate() const {
    if (hnsw_ == nullptr) {
        if (store_ != nullptr || !slot_to_chunk_.empty() || !chunk_to_slot_.empty()) {
            throw std::runtime_error("vector index has state without a graph");
        }
        return;
    }
    hnsw_->validate();
    if (store_ == nullptr) {
        throw std::runtime_error("vector index has a graph without a store");
    }
    if (slot_to_chunk_.size() != hnsw_->slot_count() ||
        chunk_to_slot_.size() != hnsw_->slot_count()) {
        throw std::runtime_error("vector index mapping size does not match the graph");
    }
    for (std::size_t slot = 0; slot < slot_to_chunk_.size(); ++slot) {
        const auto it = chunk_to_slot_.find(slot_to_chunk_[slot]);
        if (it == chunk_to_slot_.end() || it->second != slot) {
            throw std::runtime_error("vector index mapping is not bidirectional");
        }
    }
}

}  // namespace hnswhy

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "engine/hnsw.h"
#include "engine/types.h"
#include "engine/vector_store.h"
#include "product/document_store.h"

namespace hnswhy {

struct IndexedVector {
    Id chunk_id = 0;
    std::vector<float> embedding;
};

struct IndexHit {
    Id chunk_id = 0;
    float distance = 0.0f;
};

// Owns the in-memory VectorStore/HNSW pair and the only slot -> chunk mapping.
// Additions are incremental; deletions are tombstones. The application mutex
// makes all calls single-threaded.
class VectorIndex {
public:
    explicit VectorIndex(HNSWParams params = {});

    void rebuild(const std::vector<IndexedVector>& vectors);
    void add(const std::vector<IndexedVector>& vectors);
    void remove(const std::vector<Id>& chunk_ids);

    std::vector<IndexHit> search(std::span<const float> query, std::size_t k,
                                 std::size_t ef);

    std::optional<Slot> slot_for_chunk(Id chunk_id) const;
    std::optional<std::span<const float>> vector_for_chunk(Id chunk_id) const;

    std::size_t dim() const noexcept;
    std::size_t live() const noexcept;
    std::size_t slot_count() const noexcept;
    // Number of full rebuilds since construction. Uploads must use add(), so
    // tests assert this stays constant across a successful upload.
    std::size_t rebuild_count() const noexcept;

    void validate() const;

private:
    HNSWParams params_;
    std::unique_ptr<VectorStore> store_;
    std::unique_ptr<HNSW> hnsw_;
    std::vector<Id> slot_to_chunk_;
    std::unordered_map<Id, Slot> chunk_to_slot_;
    std::size_t rebuild_count_ = 0;
};

}  // namespace hnswhy

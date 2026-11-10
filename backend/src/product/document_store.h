#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace hnswhy {

using Id = std::int64_t;

struct NewChunk {
    std::string text;
    std::vector<float> embedding;
};

struct InsertResult {
    Id document_id = 0;
    std::vector<Id> chunk_ids;
};

struct DocumentSummary {
    Id id = 0;
    std::string title;
    std::string source;
    std::size_t chunk_count = 0;
    std::size_t char_count = 0;
    std::string created_at;
};

struct ChunkRecord {
    Id id = 0;
    Id document_id = 0;
    int ordinal = 0;
    std::string text;
};

struct DocumentDetail {
    DocumentSummary summary;
    std::string text;
    std::vector<ChunkRecord> chunks;
};

struct EmbeddedChunk {
    Id chunk_id = 0;
    Id document_id = 0;
    int ordinal = 0;
    std::string title;
    std::string text;
    std::vector<float> embedding;
};

struct ChunkDetail {
    Id chunk_id = 0;
    Id document_id = 0;
    std::string title;
    int ordinal = 0;
    std::string text;
};

struct ChunkText {
    Id chunk_id = 0;
    std::string text;
};

struct MapChunk {
    Id chunk_id = 0;
    Id document_id = 0;
    std::string title;
    int ordinal = 0;
    std::string text;
};

struct StoredEmbedding {
    Id chunk_id = 0;
    std::vector<float> vector;
};

struct EmbeddingMeta {
    std::string provider;
    std::string model;
    std::size_t dim = 0;

    bool empty() const noexcept { return provider.empty() && model.empty() && dim == 0; }
    bool complete() const noexcept { return !provider.empty() && !model.empty() && dim != 0; }
};

struct StoreStats {
    std::size_t documents = 0;
    std::size_t chunks = 0;
    std::size_t embeddings = 0;
};

// SQLite is the source of truth. Runtime slots, tombstones and the HNSW graph
// live only in VectorIndex; this class never exposes them.
//
// A DocumentStore is not internally synchronized. The application serializes
// calls with its own mutex and SQLite runs in serialized (FULLMUTEX) mode.
class DocumentStore {
public:
    explicit DocumentStore(const std::filesystem::path& path);
    ~DocumentStore();

    DocumentStore(const DocumentStore&) = delete;
    DocumentStore& operator=(const DocumentStore&) = delete;
    DocumentStore(DocumentStore&&) = delete;
    DocumentStore& operator=(DocumentStore&&) = delete;

    InsertResult insert_document(const std::string& title, const std::string& source,
                                 const std::string& text,
                                 const std::vector<NewChunk>& chunks);

    std::vector<DocumentSummary> list_documents() const;
    std::optional<DocumentDetail> get_document(Id document_id) const;
    std::vector<Id> chunk_ids_for_document(Id document_id) const;
    bool delete_document(Id document_id);

    // Used at startup and after a recovery rebuild. Chunks without embeddings
    // are treated as corruption and rejected.
    std::vector<EmbeddedChunk> load_embedded_chunks() const;
    std::vector<ChunkText> load_chunk_texts() const;
    std::vector<ChunkDetail> get_chunks(std::span<const Id> chunk_ids) const;
    std::vector<MapChunk> sample_chunks(std::size_t limit) const;

    EmbeddingMeta embedding_meta() const;
    void set_embedding_meta(const EmbeddingMeta& meta);
    void clear_embedding_meta();
    void replace_embeddings(std::span<const StoredEmbedding> embeddings,
                            const EmbeddingMeta& meta);

    std::optional<std::string> meta(std::string_view key) const;
    void set_meta(std::string_view key, std::string_view value);

    StoreStats stats() const;
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    void initialize_schema();
    void set_meta_internal(std::string_view key, std::string_view value);

    std::filesystem::path path_;
    sqlite3* db_ = nullptr;

    // An exclusive advisory lock held for the store's lifetime. Two server
    // processes on one SQLite file would each keep their own in-memory index
    // and silently diverge, so the second opener fails fast instead.
    struct FileLock {
        int fd = -1;
        FileLock() = default;
        ~FileLock();
        FileLock(const FileLock&) = delete;
        FileLock& operator=(const FileLock&) = delete;
    };
    FileLock lock_;
};

}  // namespace hnswhy

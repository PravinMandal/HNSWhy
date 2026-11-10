#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "product/config.h"
#include "product/document_store.h"
#include "product/rag.h"
#include "product/vector_index.h"

namespace hnswhy {

struct ProviderHealth {
    std::string provider;
    std::string model;
    bool available = false;
};

struct HealthView {
    bool ok = false;
    std::string database;
    StoreStats store;
    ProviderHealth embedding;
    ProviderHealth chat;
};

struct StatusView {
    std::string database;
    StoreStats store;
    std::size_t index_live = 0;
    std::size_t index_slots = 0;
    std::size_t index_tombstones = 0;
    std::size_t dim = 0;
    std::size_t embedding_dim = 0;
    std::string embedding_provider;
    std::string embedding_model;
    std::string chat_provider;
    std::string chat_model;
    std::size_t query_ef = 0;
    bool sample_loaded = false;
    bool benchmark_available = false;
};

struct MapPoint {
    Id chunk_id = 0;
    Id document_id = 0;
    std::string title;
    int ordinal = 0;
    std::string preview;
    std::vector<float> vector;
};

struct MapView {
    std::size_t total_chunks = 0;
    std::vector<MapPoint> points;
};

class Application {
public:
    explicit Application(AppConfig config);
    ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    DocumentSummary add_document(std::string title, std::string source,
                                 std::string text);
    std::vector<DocumentSummary> list_documents();
    std::optional<DocumentDetail> get_document(Id document_id);
    bool delete_document(Id document_id);

    RagAnswer ask(std::string_view question, std::size_t k);
    MapView map(std::size_t limit);
    StatusView status();
    HealthView health();
    std::optional<std::string> benchmark_json() const;

    const AppConfig& config() const noexcept { return config_; }

private:
    void initialize();
    DocumentSummary add_document_locked(std::string title, std::string source,
                                        std::string text);
    void build_index_from_chunks_locked(const std::vector<EmbeddedChunk>& chunks);
    void rebuild_index_locked();
    void reembed_all_locked();
    std::vector<std::vector<float>> embed_all_locked(
        const std::vector<std::string>& texts) const;
    void ensure_embedding_compatibility_locked(std::size_t actual_dim);
    void seed_sample_locked();
    void log(const std::string& message) const;

    AppConfig config_;
    DocumentStore store_;
    std::unique_ptr<EmbeddingProvider> embedder_;
    std::unique_ptr<ChatProvider> chat_;
    VectorIndex index_;
    RagService rag_;
    mutable std::mutex mutex_;
};

}  // namespace hnswhy

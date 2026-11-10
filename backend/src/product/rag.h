#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "product/document_store.h"
#include "product/providers.h"
#include "product/vector_index.h"

namespace hnswhy {

inline constexpr const char* kNoAnswer =
    "I could not find an answer to that question in the uploaded documents.";

struct RagTimings {
    double embedding_ms = 0.0;
    double search_ms = 0.0;
    double generate_ms = 0.0;
    double total_ms = 0.0;
};

struct RagSource {
    Id chunk_id = 0;
    Id document_id = 0;
    std::string title;
    int ordinal = 0;
    std::string text;
    float distance = 0.0f;
};

struct RagAnswer {
    std::string answer;
    std::vector<RagSource> sources;
    std::vector<float> query_vector;
    RagTimings timings;
    std::string embedding_provider;
    std::string embedding_model;
    std::string chat_provider;
    std::string chat_model;
};

class RagService {
public:
    RagService(const DocumentStore& store, VectorIndex& index,
               const EmbeddingProvider& embedder, const ChatProvider& chat,
               std::size_t ef_search = 50);

    RagAnswer ask(std::string_view question, std::size_t k);

private:
    const DocumentStore& store_;
    VectorIndex& index_;
    const EmbeddingProvider& embedder_;
    const ChatProvider& chat_;
    std::size_t ef_search_;
};

}  // namespace hnswhy

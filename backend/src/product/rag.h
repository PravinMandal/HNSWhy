#pragma once

#include <chrono>
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

// Validates and normalizes a question without calling the embedding provider.
// The application uses this before deciding whether an empty library can skip
// the provider round trip entirely.
std::string normalize_question(std::string_view question);

// Rejects retrieval counts outside the range the API accepts.
void validate_k(std::size_t k);

// Everything the retrieval phase needs from the question embedding step. It
// deliberately contains no store/index references so the embedding HTTP call
// can run before the application takes its database mutex.
struct RagQuery {
    std::string question;
    std::vector<float> query_vector;
    RagTimings timings;
    std::chrono::steady_clock::time_point started_at;
};

class RagService {
public:
    RagService(const DocumentStore& store, VectorIndex& index,
               const EmbeddingProvider& embedder, const ChatProvider& chat,
               std::size_t ef_search = 50);

    // Convenience path for tests and single-threaded callers. Application uses
    // the three phases below so slow provider calls never hold the mutex.
    RagAnswer ask(std::string_view question, std::size_t k);

    // A refusal that never calls the embedding or chat provider. Used when the
    // library is empty: there is nothing to search, so a remote provider
    // outage must not turn "no documents" into a 503.
    RagAnswer no_answer() const;

    // Phase 1: validate the question and call the embedding provider. Cheap to
    // run outside any lock.
    RagQuery embed_question(std::string_view question) const;

    // Phase 2: search the index and read the matching chunks. The caller must
    // hold the application mutex.
    RagAnswer retrieve(const RagQuery& query, std::size_t k);

    // Phase 3: call the chat provider using the already-copied sources. The
    // caller must not hold the mutex.
    RagAnswer generate(const RagQuery& query, RagAnswer answer) const;

private:
    const DocumentStore& store_;
    VectorIndex& index_;
    const EmbeddingProvider& embedder_;
    const ChatProvider& chat_;
    std::size_t ef_search_;
};

}  // namespace hnswhy

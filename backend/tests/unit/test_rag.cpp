#include "product/rag.h"

#include <string>
#include <vector>

#include "framework/temp_path.h"
#include "framework/test_framework.h"
#include "product/document_store.h"
#include "product/errors.h"
#include "product/providers.h"
#include "product/vector_index.h"

namespace {

using hnswhytest::TempPath;

hnswhy::DocumentStore make_store(const TempPath& db) {
    return hnswhy::DocumentStore(db.path());
}

std::vector<hnswhy::IndexedVector> index_vectors(const hnswhy::DocumentStore& store) {
    std::vector<hnswhy::IndexedVector> vectors;
    for (const hnswhy::EmbeddedChunk& chunk : store.load_embedded_chunks()) {
        vectors.push_back(hnswhy::IndexedVector{chunk.chunk_id, chunk.embedding});
    }
    return vectors;
}

class RecordingChat final : public hnswhy::ChatProvider {
public:
    std::string name() const override { return "recording"; }
    std::string model() const override { return "recording-v1"; }

    std::string answer(const hnswhy::ChatRequest& request) const override {
        ++calls;
        last = request;
        return "Grounded answer [1]";
    }

    bool available() const override { return true; }

    mutable int calls = 0;
    mutable hnswhy::ChatRequest last;
};

}  // namespace

HNSWHY_TEST(rag_answers_with_sources_and_grounded_prompt) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);
    const std::string text =
        "HNSW builds a layered proximity graph. Search descends through upper layers "
        "before exploring a candidate list on layer zero.";
    const auto embeddings = embedder.embed({text});
    const auto inserted =
        store.insert_document("HNSW", "handbook", text, {{text, embeddings[0]}});

    hnswhy::VectorIndex index;
    index.rebuild(index_vectors(store));
    RecordingChat chat;
    hnswhy::RagService rag(store, index, embedder, chat, 16);

    const hnswhy::RagAnswer answer = rag.ask("How does HNSW search work?", 2);
    CHECK_EQ(answer.answer, std::string("Grounded answer [1]"));
    CHECK_EQ(chat.calls, 1);
    CHECK_EQ(answer.sources.size(), std::size_t{1});
    CHECK_EQ(answer.sources[0].chunk_id, inserted.chunk_ids[0]);
    CHECK_EQ(answer.sources[0].document_id, inserted.document_id);
    CHECK_EQ(answer.sources[0].title, std::string("HNSW"));
    CHECK_EQ(answer.embedding_provider, std::string("hash"));
    CHECK_EQ(answer.embedding_model, std::string("local-hash-v1"));
    CHECK_EQ(answer.chat_provider, std::string("recording"));

    CHECK(chat.last.system_prompt.find("only the numbered sources") != std::string::npos);
    CHECK(chat.last.user_prompt.find("Question: How does HNSW search work?") !=
          std::string::npos);
    CHECK(chat.last.user_prompt.find("[1] HNSW") != std::string::npos);
    CHECK_EQ(chat.last.sources.size(), std::size_t{1});
    CHECK(answer.timings.total_ms >= 0.0);
    CHECK(answer.timings.embedding_ms >= 0.0);
    CHECK(answer.timings.search_ms >= 0.0);
    CHECK(answer.timings.generate_ms >= 0.0);
}

HNSWHY_TEST(rag_refuses_without_documents_and_skips_chat) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);
    hnswhy::VectorIndex index;
    RecordingChat chat;
    hnswhy::RagService rag(store, index, embedder, chat, 16);

    const hnswhy::RagAnswer answer = rag.ask("What is HNSW?", 3);
    CHECK_EQ(answer.answer, std::string(hnswhy::kNoAnswer));
    CHECK(answer.sources.empty());
    CHECK_EQ(chat.calls, 0);
}

HNSWHY_TEST(rag_offline_pipeline_cites_retrieved_sentence) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);
    hnswhy::ExtractiveChatProvider chat;
    const std::string text =
        "The efSearch parameter controls the size of the candidate list during a query. "
        "A larger candidate list usually improves recall but increases query latency. "
        "HNSW stores vectors in a proximity graph rather than a flat array.";
    const auto embeddings = embedder.embed({text});
    store.insert_document("Search tuning", "notes", text, {{text, embeddings[0]}});

    hnswhy::VectorIndex index;
    index.rebuild(index_vectors(store));
    hnswhy::RagService rag(store, index, embedder, chat, 16);

    const hnswhy::RagAnswer answer = rag.ask("What does efSearch control?", 3);
    CHECK(answer.answer.find("efSearch") != std::string::npos);
    CHECK(answer.answer.find("candidate list") != std::string::npos);
    CHECK(answer.answer.find("[1]") != std::string::npos);
    CHECK(!answer.sources.empty());
}

HNSWHY_TEST(rag_validates_question_and_k) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);
    hnswhy::VectorIndex index;
    RecordingChat chat;
    hnswhy::RagService rag(store, index, embedder, chat, 16);

    CHECK_THROWS_AS(rag.ask("   ", 3), hnswhy::ProductError);
    CHECK_THROWS_AS(rag.ask(std::string(2001, 'a'), 3), hnswhy::ProductError);
    CHECK_THROWS_AS(rag.ask("question", 0), hnswhy::ProductError);
    CHECK_THROWS_AS(rag.ask("question", 21), hnswhy::ProductError);
    CHECK_EQ(chat.calls, 0);
}

HNSWHY_TEST(rag_detects_stale_index_entries) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);
    hnswhy::VectorIndex index;
    index.rebuild({{999, embedder.embed({"orphan chunk"}).front()}});
    RecordingChat chat;
    hnswhy::RagService rag(store, index, embedder, chat, 16);

    CHECK_THROWS_AS(rag.ask("orphan chunk", 1), hnswhy::ProductError);
}

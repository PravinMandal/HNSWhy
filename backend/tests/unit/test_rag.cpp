#include "product/rag.h"

#include <limits>
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
        return response;
    }

    bool available() const override { return true; }

    std::string response = "Grounded answer [1]";
    mutable int calls = 0;
    mutable hnswhy::ChatRequest last;
};

class FixedEmbeddingProvider final : public hnswhy::EmbeddingProvider {
public:
    explicit FixedEmbeddingProvider(std::size_t dim) : dim_(dim) {}

    std::string name() const override { return "fixed"; }
    std::string model() const override { return "fixed-v1"; }
    std::size_t dim() const override { return dim_; }

    std::vector<std::vector<float>> embed(
        const std::vector<std::string>& texts) const override {
        std::vector<std::vector<float>> result;
        result.reserve(texts.size());
        for (std::size_t i = 0; i < texts.size(); ++i) {
            std::vector<float> row(dim_, 0.0f);
            row[0] = 1.0f;
            result.push_back(std::move(row));
        }
        return result;
    }

    bool available() const override { return true; }

private:
    std::size_t dim_;
};

class NonFiniteEmbeddingProvider final : public hnswhy::EmbeddingProvider {
public:
    explicit NonFiniteEmbeddingProvider(float value) : value_(value) {}

    std::string name() const override { return "non-finite"; }
    std::string model() const override { return "test"; }
    std::size_t dim() const override { return 2; }

    std::vector<std::vector<float>> embed(
        const std::vector<std::string>& texts) const override {
        return std::vector<std::vector<float>>(
            texts.size(), std::vector<float>{value_, 0.0f});
    }

    bool available() const override { return true; }

private:
    float value_;
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

HNSWHY_TEST(rag_offline_pipeline_preserves_decimal_values) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);
    hnswhy::ExtractiveChatProvider chat;
    const std::string text =
        "At efSearch 10 the measured recall at ten was 0.7998 on the SIFT fixture.";
    const auto embeddings = embedder.embed({text});
    store.insert_document("Benchmark", "notes", text, {{text, embeddings[0]}});

    hnswhy::VectorIndex index;
    index.rebuild(index_vectors(store));
    hnswhy::RagService rag(store, index, embedder, chat, 16);

    const hnswhy::RagAnswer answer = rag.ask("What recall did efSearch 10 measure?", 2);
    CHECK(answer.answer.find("0.7998") != std::string::npos);
    CHECK(answer.answer.find("[1]") != std::string::npos);
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

HNSWHY_TEST(rag_rejects_non_finite_query_embeddings) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::VectorIndex index;
    index.rebuild({{1, {1.0f, 0.0f}}});
    RecordingChat chat;

    for (const float value : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()}) {
        NonFiniteEmbeddingProvider embedder(value);
        hnswhy::RagService rag(store, index, embedder, chat, 16);
        CHECK_THROWS_AS(rag.ask("question", 1), hnswhy::ProductError);
    }
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

HNSWHY_TEST(rag_ranks_the_relevant_document_first) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);

    const std::string relevant =
        "efSearch controls the size of the candidate list during an HNSW query. "
        "A larger efSearch usually improves recall but increases query latency.";
    const std::string cooking =
        "Bananas are a yellow fruit. Mash them with flour and bake the batter.";
    const std::string finance =
        "Quarterly revenue grew after the company reorganized its sales regions.";

    const auto relevant_doc = store.insert_document(
        "Search tuning", "search.md", relevant, {{relevant, embedder.embed({relevant}).front()}});
    const auto cooking_doc = store.insert_document(
        "Cooking", "cooking.md", cooking, {{cooking, embedder.embed({cooking}).front()}});
    const auto finance_doc = store.insert_document(
        "Finance", "finance.md", finance, {{finance, embedder.embed({finance}).front()}});

    hnswhy::VectorIndex index;
    index.rebuild(index_vectors(store));
    RecordingChat chat;
    hnswhy::RagService rag(store, index, embedder, chat, 3);

    const hnswhy::RagAnswer answer = rag.ask("What does efSearch control?", 3);
    CHECK(!answer.sources.empty());
    CHECK_EQ(answer.sources[0].document_id, relevant_doc.document_id);
    CHECK(answer.sources[0].document_id != cooking_doc.document_id);
    CHECK(answer.sources[0].document_id != finance_doc.document_id);

    // The prompt numbers sources in retrieval order, so source [1] must be the
    // same document the answer cites.
    CHECK(chat.last.user_prompt.find("[1] Search tuning") != std::string::npos);
    CHECK(chat.last.user_prompt.find("What does efSearch control?") != std::string::npos);
}

HNSWHY_TEST(rag_returns_uncited_chat_text_unchanged) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);
    const std::string text =
        "HNSW stores vectors in a layered proximity graph and descends upper layers.";
    store.insert_document("Graph", "graph.md", text, {{text, embedder.embed({text}).front()}});

    hnswhy::VectorIndex index;
    index.rebuild(index_vectors(store));
    RecordingChat chat;
    chat.response = "An answer without any citation.";
    hnswhy::RagService rag(store, index, embedder, chat, 16);

    // The backend does not rewrite or reject model text; the frontend shows an
    // "uncited" warning so the user can judge it. This test pins that policy.
    const hnswhy::RagAnswer answer = rag.ask("How does HNSW store vectors?", 1);
    CHECK_EQ(answer.answer, std::string("An answer without any citation."));
    CHECK(!answer.sources.empty());
}

HNSWHY_TEST(rag_rejects_an_empty_chat_answer) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::HashEmbeddingProvider embedder(256);
    const std::string text = "HNSW uses a graph of vectors rather than a flat list.";
    store.insert_document("Graph", "graph.md", text, {{text, embedder.embed({text}).front()}});

    hnswhy::VectorIndex index;
    index.rebuild(index_vectors(store));
    RecordingChat chat;
    chat.response = "";
    hnswhy::RagService rag(store, index, embedder, chat, 16);

    try {
        (void)rag.ask("How does HNSW store vectors?", 1);
        CHECK(false);
    } catch (const hnswhy::ProductError& error) {
        CHECK_EQ(error.code(), std::string("PROVIDER_ERROR"));
    }
}

HNSWHY_TEST(rag_detects_query_embedding_dimension_mismatch) {
    TempPath db(".db");
    hnswhy::DocumentStore store = make_store(db);
    hnswhy::VectorIndex index;
    index.rebuild({{1, {1.0f, 0.0f}}});
    RecordingChat chat;
    FixedEmbeddingProvider query_embedder(3);
    hnswhy::RagService rag(store, index, query_embedder, chat, 16);

    try {
        (void)rag.ask("question", 1);
        CHECK(false);
    } catch (const hnswhy::ProductError& error) {
        CHECK_EQ(error.code(), std::string("EMBEDDING_MISMATCH"));
    }
    CHECK_EQ(chat.calls, 0);
}

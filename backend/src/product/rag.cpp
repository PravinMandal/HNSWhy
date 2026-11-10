#include "product/rag.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include "product/errors.h"
#include "product/chunker.h"
#include "product/text.h"

namespace hnswhy {
namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

std::string shorten(std::string_view text, std::size_t limit) {
    return truncate_utf8_words(text, limit);
}

std::string build_system_prompt() {
    return "You are HNSWhy, a retrieval-augmented question answering assistant.\n"
           "Answer the question using only the numbered sources supplied by the user.\n"
           "Every factual claim must cite a source as [1], [2], and so on.\n"
           "If the sources do not contain the answer, say exactly:\n\"" +
           std::string(kNoAnswer) +
           "\"\n"
           "Do not use outside knowledge and do not invent citations.";
}

std::string build_user_prompt(std::string_view question,
                              const std::vector<RagSource>& sources) {
    std::string prompt = "Question: ";
    prompt += question;
    prompt += "\n\nSources:\n";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        prompt += "[";
        prompt += std::to_string(i + 1);
        prompt += "] ";
        prompt += sources[i].title;
        prompt += "\n";
        prompt += shorten(sources[i].text, 1600);
        prompt += "\n\n";
    }
    prompt += "Answer with citations. If the answer is not present, use the exact refusal sentence.";
    return prompt;
}

}  // namespace

RagService::RagService(const DocumentStore& store, VectorIndex& index,
                       const EmbeddingProvider& embedder, const ChatProvider& chat,
                       std::size_t ef_search)
    : store_(store), index_(index), embedder_(embedder), chat_(chat), ef_search_(ef_search) {
    if (ef_search_ == 0) {
        ef_search_ = 50;
    }
}

RagAnswer RagService::ask(std::string_view question, std::size_t k) {
    const std::string trimmed = trim_copy(question);
    if (trimmed.empty()) {
        throw validation_error("question must not be empty");
    }
    if (!is_valid_utf8(trimmed)) {
        throw validation_error("question must be valid UTF-8");
    }
    if (utf8_length(trimmed) > 2000) {
        throw validation_error("question must be at most 2000 characters");
    }
    if (k == 0 || k > 20) {
        throw validation_error("k must be between 1 and 20");
    }

    const Clock::time_point total_begin = Clock::now();
    RagAnswer answer;
    answer.embedding_provider = embedder_.name();
    answer.embedding_model = embedder_.model();
    answer.chat_provider = chat_.name();
    answer.chat_model = chat_.model();

    if (index_.live() == 0) {
        answer.answer = kNoAnswer;
        return answer;
    }

    const Clock::time_point embed_begin = Clock::now();
    std::vector<std::vector<float>> query_embeddings = embedder_.embed({trimmed});
    answer.timings.embedding_ms = elapsed_ms(embed_begin);
    if (query_embeddings.size() != 1 || query_embeddings[0].empty()) {
        throw ProductError("PROVIDER_ERROR", "the embedding provider returned no query vector",
                           502);
    }
    std::vector<float> query = normalize_embedding(std::move(query_embeddings[0]));
    if (query.size() != index_.dim()) {
        throw ProductError("EMBEDDING_MISMATCH",
                           "the question embedding dimension does not match the index", 500);
    }
    answer.query_vector = query;

    const Clock::time_point search_begin = Clock::now();
    const std::vector<IndexHit> hits = index_.search(query, k, ef_search_);
    answer.timings.search_ms = elapsed_ms(search_begin);
    if (hits.empty()) {
        answer.answer = kNoAnswer;
        answer.timings.total_ms = elapsed_ms(total_begin);
        return answer;
    }

    std::vector<Id> chunk_ids;
    chunk_ids.reserve(hits.size());
    for (const IndexHit& hit : hits) {
        chunk_ids.push_back(hit.chunk_id);
    }
    const std::vector<ChunkDetail> chunks = store_.get_chunks(chunk_ids);
    if (chunks.size() != hits.size()) {
        throw ProductError("INDEX_STALE",
                           "the search index references chunks that are not in the database",
                           500);
    }

    answer.sources.reserve(hits.size());
    for (std::size_t i = 0; i < hits.size(); ++i) {
        RagSource source;
        source.chunk_id = chunks[i].chunk_id;
        source.document_id = chunks[i].document_id;
        source.title = chunks[i].title;
        source.ordinal = chunks[i].ordinal;
        source.text = chunks[i].text;
        source.distance = hits[i].distance;
        answer.sources.push_back(std::move(source));
    }

    ChatRequest request;
    request.question = trimmed;
    request.system_prompt = build_system_prompt();
    request.user_prompt = build_user_prompt(trimmed, answer.sources);
    for (const RagSource& source : answer.sources) {
        request.sources.push_back(ChatSource{source.title, source.text});
    }

    const Clock::time_point generate_begin = Clock::now();
    answer.answer = chat_.answer(request);
    answer.timings.generate_ms = elapsed_ms(generate_begin);
    if (answer.answer.empty()) {
        throw ProductError("PROVIDER_ERROR", "the chat provider returned an empty answer", 502);
    }
    answer.timings.total_ms = elapsed_ms(total_begin);
    return answer;
}

}  // namespace hnswhy

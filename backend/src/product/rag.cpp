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
    return "You are HNSWhy, a helpful question answering assistant.\n"
           "You may receive numbered document sources. Use them when they are relevant, "
           "and cite the sources you use as [1], [2], and so on.\n"
           "A source heading is part of that source: a pronoun in the body may refer to "
           "the subject in the heading.\n"
           "If the sources do not answer the question, answer from your own general "
           "knowledge. Do not cite a source you did not use, and never invent citations.\n"
           "Only cite a source when that source directly supports the sentence. If you "
           "answer from general knowledge, do not include any citation markers.\n"
           "Do not claim that a source says something it does not.\n"
           "Copy numbers, names, and dates exactly as they appear in a source; never "
           "substitute a number from a different sentence.\n"
           "Be concise: at most three sentences unless the question asks for detail, and "
           "do not repeat source paragraphs verbatim.";
}

std::string build_user_prompt(std::string_view question,
                              const std::vector<RagSource>& sources) {
    std::string prompt = "Question: ";
    prompt += question;
    prompt += "\n\n";
    if (sources.empty()) {
        prompt += "No document chunks were retrieved.\n\n"
                  "Answer the question from general knowledge. Do not cite a source.";
        return prompt;
    }

    prompt += "Sources:\n";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        prompt += "[";
        prompt += std::to_string(i + 1);
        prompt += "] ";
        prompt += sources[i].title;
        prompt += "\n";
        prompt += shorten(sources[i].text, 1600);
        prompt += "\n\n";
    }
    prompt += "Answer the question. Cite the sources you use. If none of the sources mention "
              "the question's subject, ignore them completely and answer from general "
              "knowledge without citations.";
    return prompt;
}

}  // namespace

std::string normalize_question(std::string_view question) {
    std::string normalized = trim_copy(question);
    if (normalized.empty()) {
        throw validation_error("question must not be empty");
    }
    if (!is_valid_utf8(normalized)) {
        throw validation_error("question must be valid UTF-8");
    }
    if (utf8_length(normalized) > 2000) {
        throw validation_error("question must be at most 2000 characters");
    }
    return normalized;
}

void validate_k(std::size_t k) {
    if (k == 0 || k > 20) {
        throw validation_error("k must be between 1 and 20");
    }
}

RagService::RagService(const DocumentStore& store, VectorIndex& index,
                       const EmbeddingProvider& embedder, const ChatProvider& chat,
                       std::size_t ef_search, double min_source_similarity)
    : store_(store),
      index_(index),
      embedder_(embedder),
      chat_(chat),
      ef_search_(ef_search),
      min_source_similarity_(min_source_similarity) {
    if (ef_search_ == 0) {
        ef_search_ = 50;
    }
    if (min_source_similarity_ < 0.0 || min_source_similarity_ > 1.0) {
        throw ProductError("CONFIG_ERROR",
                           "min_source_similarity must be between 0 and 1", 500);
    }
}

RagQuery RagService::prepare_question(std::string_view question) const {
    RagQuery query;
    query.question = normalize_question(question);
    query.started_at = Clock::now();
    return query;
}

RagQuery RagService::embed_question(std::string_view question) const {
    RagQuery query;
    query.question = normalize_question(question);

    query.started_at = Clock::now();
    const Clock::time_point embed_begin = Clock::now();
    std::vector<std::vector<float>> query_embeddings = embedder_.embed({query.question});
    query.timings.embedding_ms = elapsed_ms(embed_begin);
    if (query_embeddings.size() != 1 || query_embeddings[0].empty()) {
        throw ProductError("PROVIDER_ERROR", "the embedding provider returned no query vector",
                           502);
    }
    if (!finite_embedding(query_embeddings[0])) {
        throw ProductError("PROVIDER_ERROR",
                           "the embedding provider returned a non-finite query vector", 502);
    }
    query.query_vector = normalize_embedding(std::move(query_embeddings[0]));
    return query;
}

RagAnswer RagService::retrieve(const RagQuery& query, std::size_t k) {
    validate_k(k);

    RagAnswer answer;
    answer.embedding_provider = embedder_.name();
    answer.embedding_model = embedder_.model();
    answer.chat_provider = chat_.name();
    answer.chat_model = chat_.model();
    answer.query_vector = query.query_vector;
    answer.timings = query.timings;

    if (index_.live() == 0) {
        answer.timings.total_ms = elapsed_ms(query.started_at);
        return answer;
    }

    if (query.query_vector.size() != index_.dim()) {
        throw ProductError("EMBEDDING_MISMATCH",
                           "the question embedding dimension does not match the index", 500);
    }

    const Clock::time_point search_begin = Clock::now();
    const std::vector<IndexHit> hits = index_.search(query.query_vector, k, ef_search_);
    answer.timings.search_ms = elapsed_ms(search_begin);
    if (hits.empty()) {
        answer.timings.total_ms = elapsed_ms(query.started_at);
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
        // Embeddings are normalized, so squared L2 maps to cosine similarity:
        // d = 2 - 2*cos. A source below the floor is unrelated context and is
        // better omitted than handed to a small model as a tempting citation.
        const double similarity = 1.0 - static_cast<double>(hits[i].distance) / 2.0;
        if (min_source_similarity_ > 0.0 && similarity < min_source_similarity_) {
            continue;
        }
        RagSource source;
        source.chunk_id = chunks[i].chunk_id;
        source.document_id = chunks[i].document_id;
        source.title = chunks[i].title;
        source.ordinal = chunks[i].ordinal;
        source.text = chunks[i].text;
        source.distance = hits[i].distance;
        answer.sources.push_back(std::move(source));
    }
    answer.timings.total_ms = elapsed_ms(query.started_at);
    return answer;
}

RagAnswer RagService::generate(const RagQuery& query, RagAnswer answer) const {
    answer.embedding_provider = embedder_.name();
    answer.embedding_model = embedder_.model();
    answer.chat_provider = chat_.name();
    answer.chat_model = chat_.model();
    if (answer.query_vector.empty()) {
        answer.query_vector = query.query_vector;
    }
    if (answer.timings.total_ms == 0.0) {
        answer.timings = query.timings;
    }

    ChatRequest request;
    request.question = query.question;
    request.system_prompt = build_system_prompt();
    request.user_prompt = build_user_prompt(query.question, answer.sources);
    for (const RagSource& source : answer.sources) {
        request.sources.push_back(ChatSource{source.title, source.text});
    }

    const Clock::time_point generate_begin = Clock::now();
    answer.answer = chat_.answer(request);
    answer.timings.generate_ms = elapsed_ms(generate_begin);
    if (answer.answer.empty()) {
        throw ProductError("PROVIDER_ERROR", "the chat provider returned an empty answer", 502);
    }
    answer.timings.total_ms = elapsed_ms(query.started_at);
    return answer;
}

RagAnswer RagService::ask(std::string_view question, std::size_t k) {
    const RagQuery query = embed_question(question);
    RagAnswer answer = retrieve(query, k);
    return generate(query, std::move(answer));
}

}  // namespace hnswhy

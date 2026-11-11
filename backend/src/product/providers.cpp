#include "product/providers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "json.hpp"
#include "product/errors.h"
#include "product/http.h"
#include "product/ollama_client.h"
#include "product/text.h"

namespace hnswhy {
namespace {

using json = nlohmann::json;

std::string lower_trim(std::string value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    value = value.substr(begin, end - begin);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<float> embed_hash(std::string_view text, std::size_t dim) {
    std::vector<float> vector(dim, 0.0f);
    const std::vector<std::string> tokens = content_terms(text);
    std::string previous;
    for (const std::string& token : tokens) {
        const std::uint64_t hash = fnv1a(token);
        vector[static_cast<std::size_t>(hash % dim)] += 1.0f;

        if (!previous.empty()) {
            const std::string bigram = previous + " " + token;
            const std::uint64_t bigram_hash = fnv1a(bigram);
            vector[static_cast<std::size_t>(bigram_hash % dim)] += 0.5f;
        }
        previous = token;
    }
    return normalize_embedding(std::move(vector));
}

json parse_provider_json(const HttpResult& response, const std::string& context) {
    if (response.status < 200 || response.status >= 300) {
        throw ProductError("PROVIDER_ERROR",
                           context + " returned HTTP " + std::to_string(response.status) +
                               ": " + short_body(response.body),
                           502);
    }
    try {
        return json::parse(response.body);
    } catch (const std::exception&) {
        throw ProductError("PROVIDER_ERROR", context + " returned invalid JSON", 502);
    }
}

std::vector<float> json_embedding(const json& value, const std::string& context) {
    if (!value.is_array() || value.empty()) {
        throw ProductError("PROVIDER_ERROR", context + " returned an empty embedding", 502);
    }
    std::vector<float> result;
    result.reserve(value.size());
    for (const json& item : value) {
        if (!item.is_number()) {
            throw ProductError("PROVIDER_ERROR", context + " returned a non-numeric embedding",
                               502);
        }
        result.push_back(item.get<float>());
    }
    return result;
}

class OllamaEmbeddingProvider final : public EmbeddingProvider {
public:
    explicit OllamaEmbeddingProvider(EmbeddingConfig config)
        : client_(config.base_url, config.timeout_seconds),
          model_(std::move(config.model)),
          configured_dim_(config.dim) {}

    std::string name() const override { return "ollama"; }
    std::string model() const override { return model_; }
    std::size_t dim() const override { return configured_dim_; }

    std::vector<std::vector<float>> embed(
        const std::vector<std::string>& texts) const override {
        std::vector<std::vector<float>> embeddings = client_.embed(model_, texts);
        if (embeddings.size() != texts.size()) {
            throw ProductError("PROVIDER_ERROR",
                               "Ollama returned a different number of embeddings", 502);
        }
        std::size_t dim = 0;
        for (auto& embedding : embeddings) {
            if (!finite_embedding(embedding)) {
                throw ProductError("PROVIDER_ERROR",
                                   "Ollama returned a non-finite embedding", 502);
            }
            if (dim == 0) {
                dim = embedding.size();
            } else if (embedding.size() != dim) {
                throw ProductError("PROVIDER_ERROR",
                                   "Ollama returned inconsistent embedding dimensions", 502);
            }
            embedding = normalize_embedding(std::move(embedding));
        }
        return embeddings;
    }

    bool available() const override { return client_.reachable(); }

private:
    OllamaClient client_;
    std::string model_;
    std::size_t configured_dim_;
};

class OllamaChatProvider final : public ChatProvider {
public:
    explicit OllamaChatProvider(ChatConfig config)
        : client_(config.base_url, config.timeout_seconds), model_(std::move(config.model)) {}

    std::string name() const override { return "ollama"; }
    std::string model() const override { return model_; }

    std::string answer(const ChatRequest& request) const override {
        std::string prompt = request.system_prompt;
        if (!request.user_prompt.empty()) {
            if (!prompt.empty()) {
                prompt += "\n\n";
            }
            prompt += request.user_prompt;
        }
        return client_.generate(model_, prompt);
    }

    bool available() const override { return client_.reachable(); }

private:
    OllamaClient client_;
    std::string model_;
};

class OpenAICompatibleEmbeddingProvider final : public EmbeddingProvider {
public:
    explicit OpenAICompatibleEmbeddingProvider(EmbeddingConfig config)
        : endpoint_(parse_endpoint(config.base_url)),
          api_key_(std::move(config.api_key)),
          model_(std::move(config.model)),
          configured_dim_(config.dim),
          timeout_seconds_(config.timeout_seconds) {}

    std::string name() const override { return "openai"; }
    std::string model() const override { return model_; }
    std::size_t dim() const override { return configured_dim_; }

    std::vector<std::vector<float>> embed(
        const std::vector<std::string>& texts) const override {
        if (texts.empty()) {
            return {};
        }
        const json body = {{"model", model_}, {"input", texts}};
        const json parsed = parse_provider_json(
            http_post(endpoint_, "/embeddings", body.dump(), headers(), timeout_seconds_),
            "OpenAI-compatible /embeddings");
        if (!parsed.contains("data") || !parsed["data"].is_array() ||
            parsed["data"].size() != texts.size()) {
            throw ProductError("PROVIDER_ERROR",
                               "OpenAI-compatible /embeddings returned a bad data array", 502);
        }

        std::vector<std::pair<std::size_t, std::vector<float>>> ordered;
        ordered.reserve(texts.size());
        std::vector<bool> seen(texts.size(), false);
        for (std::size_t i = 0; i < parsed["data"].size(); ++i) {
            const json& item = parsed["data"][i];
            if (!item.contains("embedding")) {
                throw ProductError("PROVIDER_ERROR",
                                   "OpenAI-compatible response has no embedding", 502);
            }
            std::vector<float> embedding =
                json_embedding(item["embedding"], "OpenAI-compatible /embeddings");
            std::size_t index = i;
            if (item.contains("index")) {
                if (!item["index"].is_number_integer()) {
                    throw ProductError("PROVIDER_ERROR",
                                       "OpenAI-compatible response has a non-integer index", 502);
                }
                // Read as int64 and range-check before narrowing. get<int>()
                // would throw nlohmann's own out_of_range and surface as a 500
                // internal error for a malformed provider response.
                const long long raw = item["index"].get<long long>();
                if (raw < 0 || static_cast<unsigned long long>(raw) >= texts.size()) {
                    throw ProductError(
                        "PROVIDER_ERROR",
                        "OpenAI-compatible response has a duplicate or out-of-range index", 502);
                }
                index = static_cast<std::size_t>(raw);
            }
            if (seen[index]) {
                throw ProductError("PROVIDER_ERROR",
                                   "OpenAI-compatible response has a duplicate or out-of-range index",
                                   502);
            }
            seen[index] = true;
            ordered.emplace_back(index, std::move(embedding));
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<std::vector<float>> result;
        result.reserve(ordered.size());
        std::size_t dim = 0;
        for (auto& [index, embedding] : ordered) {
            (void)index;
            if (!finite_embedding(embedding)) {
                throw ProductError("PROVIDER_ERROR",
                                   "OpenAI-compatible provider returned a non-finite embedding",
                                   502);
            }
            if (dim == 0) {
                dim = embedding.size();
            } else if (embedding.size() != dim) {
                throw ProductError("PROVIDER_ERROR",
                                   "OpenAI-compatible provider returned inconsistent dimensions",
                                   502);
            }
            result.push_back(normalize_embedding(std::move(embedding)));
        }
        return result;
    }

    bool available() const override {
        try {
            const HttpResult response = http_get(endpoint_, "/models", headers(), 2);
            return response.status >= 200 && response.status < 300;
        } catch (const std::exception&) {
            return false;
        }
    }

private:
    std::vector<std::pair<std::string, std::string>> headers() const {
        if (api_key_.empty()) {
            return {};
        }
        return {{"Authorization", "Bearer " + api_key_}};
    }

    HttpEndpoint endpoint_;
    std::string api_key_;
    std::string model_;
    std::size_t configured_dim_;
    int timeout_seconds_;
};

class OpenAICompatibleChatProvider final : public ChatProvider {
public:
    explicit OpenAICompatibleChatProvider(ChatConfig config)
        : endpoint_(parse_endpoint(config.base_url)),
          api_key_(std::move(config.api_key)),
          model_(std::move(config.model)),
          timeout_seconds_(config.timeout_seconds) {}

    std::string name() const override { return "openai"; }
    std::string model() const override { return model_; }

    std::string answer(const ChatRequest& request) const override {
        const json body = {
            {"model", model_},
            {"temperature", 0.2},
            {"messages",
             json::array({{{"role", "system"}, {"content", request.system_prompt}},
                          {{"role", "user"}, {"content", request.user_prompt}}})}};
        const json parsed = parse_provider_json(
            http_post(endpoint_, "/chat/completions", body.dump(), headers(), timeout_seconds_),
            "OpenAI-compatible /chat/completions");
        if (!parsed.contains("choices") || !parsed["choices"].is_array() ||
            parsed["choices"].empty()) {
            throw ProductError("PROVIDER_ERROR",
                               "OpenAI-compatible chat response has no choices", 502);
        }
        const json& choice = parsed["choices"][0];
        if (!choice.contains("message") || !choice["message"].contains("content") ||
            !choice["message"]["content"].is_string()) {
            throw ProductError("PROVIDER_ERROR",
                               "OpenAI-compatible chat response has no content", 502);
        }
        const std::string result = choice["message"]["content"].get<std::string>();
        if (result.empty()) {
            throw ProductError("PROVIDER_ERROR",
                               "OpenAI-compatible chat returned an empty answer", 502);
        }
        return result;
    }

    bool available() const override {
        try {
            const HttpResult response = http_get(endpoint_, "/models", headers(), 2);
            return response.status >= 200 && response.status < 300;
        } catch (const std::exception&) {
            return false;
        }
    }

private:
    std::vector<std::pair<std::string, std::string>> headers() const {
        if (api_key_.empty()) {
            return {};
        }
        return {{"Authorization", "Bearer " + api_key_}};
    }

    HttpEndpoint endpoint_;
    std::string api_key_;
    std::string model_;
    int timeout_seconds_;
};

std::string shorten_sentence(std::string sentence, std::size_t limit = 360) {
    return truncate_utf8_words(sentence, limit);
}

bool is_markdown_heading(std::string_view sentence) {
    std::size_t begin = 0;
    while (begin < sentence.size() &&
           std::isspace(static_cast<unsigned char>(sentence[begin])) != 0) {
        ++begin;
    }
    return begin < sentence.size() && sentence[begin] == '#';
}

}  // namespace

HashEmbeddingProvider::HashEmbeddingProvider(std::size_t dim, std::string model)
    : dim_(dim), model_(std::move(model)) {
    if (dim_ == 0 || dim_ > 65536) {
        throw std::invalid_argument("hash embedding dimension is out of range");
    }
    if (model_.empty()) {
        model_ = "local-hash-v1";
    }
}

std::vector<std::vector<float>> HashEmbeddingProvider::embed(
    const std::vector<std::string>& texts) const {
    std::vector<std::vector<float>> result;
    result.reserve(texts.size());
    for (const std::string& text : texts) {
        result.push_back(embed_hash(text, dim_));
    }
    return result;
}

ExtractiveChatProvider::ExtractiveChatProvider(std::string model)
    : model_(std::move(model)) {
    if (model_.empty()) {
        model_ = "local-extractive-v1";
    }
}

std::string ExtractiveChatProvider::answer(const ChatRequest& request) const {
    const std::vector<std::string> question_terms = content_terms(request.question);
    std::unordered_set<std::string> wanted(question_terms.begin(), question_terms.end());
    if (wanted.empty()) {
        return "I could not find an answer to that question in the uploaded documents.";
    }

    struct Candidate {
        float score = 0.0f;
        std::size_t source = 0;
        std::string sentence;
    };
    std::vector<Candidate> candidates;
    for (std::size_t source = 0; source < request.sources.size(); ++source) {
        std::vector<std::string> sentences = split_sentences(request.sources[source].text);
        if (sentences.empty()) {
            sentences.push_back(request.sources[source].text);
        }
        for (std::string& sentence : sentences) {
            if (is_markdown_heading(sentence)) {
                continue;
            }
            const std::vector<std::string> terms = content_terms(sentence);
            std::unordered_set<std::string> present(terms.begin(), terms.end());
            std::size_t overlap = 0;
            for (const std::string& term : wanted) {
                if (present.count(term) != 0) {
                    ++overlap;
                }
            }
            if (overlap == 0) {
                continue;
            }
            const float score = static_cast<float>(overlap) /
                                std::sqrt(static_cast<float>(terms.size() + 1));
            candidates.push_back(Candidate{score, source, shorten_sentence(sentence)});
        }
    }

    if (candidates.empty()) {
        return "I could not find an answer to that question in the uploaded documents.";
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.score != b.score) {
                      return a.score > b.score;
                  }
                  return a.sentence.size() < b.sentence.size();
              });

    std::string answer = "Based on the uploaded documents:\n\n";
    std::unordered_set<std::string> emitted;
    std::size_t used = 0;
    for (const Candidate& candidate : candidates) {
        if (emitted.count(candidate.sentence) != 0) {
            continue;
        }
        emitted.insert(candidate.sentence);
        answer += "- " + candidate.sentence + " [" +
                  std::to_string(candidate.source + 1) + "]\n";
        if (++used == 3) {
            break;
        }
    }
    return answer;
}

std::unique_ptr<EmbeddingProvider> create_embedding_provider(
    const EmbeddingConfig& config) {
    const std::string provider = lower_trim(config.provider);
    if (provider.empty() || provider == "hash") {
        std::size_t dim = config.dim == 0 ? 256 : config.dim;
        if (dim > 65536) {
            throw ProductError("PROVIDER_CONFIG",
                               "embedding dimension must be between 1 and 65536", 500);
        }
        return std::make_unique<HashEmbeddingProvider>(
            dim, config.model.empty() ? "local-hash-v1" : config.model);
    }

    if (provider == "ollama") {
        EmbeddingConfig resolved = config;
        if (resolved.model.empty()) {
            resolved.model = "nomic-embed-text";
        }
        if (resolved.base_url.empty()) {
            resolved.base_url = "http://127.0.0.1:11434";
        }
        if (resolved.timeout_seconds < 1) {
            resolved.timeout_seconds = 60;
        }
        return std::make_unique<OllamaEmbeddingProvider>(std::move(resolved));
    }

    if (provider == "openai") {
        EmbeddingConfig resolved = config;
        if (resolved.model.empty()) {
            resolved.model = "text-embedding-3-small";
        }
        if (resolved.base_url.empty()) {
            resolved.base_url = "https://api.openai.com/v1";
        }
        if (resolved.timeout_seconds < 1) {
            resolved.timeout_seconds = 60;
        }
        return std::make_unique<OpenAICompatibleEmbeddingProvider>(std::move(resolved));
    }

    throw ProductError("PROVIDER_CONFIG",
                       "unknown embedding provider: " + config.provider, 500);
}

std::unique_ptr<ChatProvider> create_chat_provider(const ChatConfig& config) {
    const std::string provider = lower_trim(config.provider);
    if (provider.empty() || provider == "extractive") {
        return std::make_unique<ExtractiveChatProvider>(
            config.model.empty() ? "local-extractive-v1" : config.model);
    }

    if (provider == "ollama") {
        ChatConfig resolved = config;
        if (resolved.model.empty()) {
            resolved.model = "llama3.2";
        }
        if (resolved.base_url.empty()) {
            resolved.base_url = "http://127.0.0.1:11434";
        }
        if (resolved.timeout_seconds < 1) {
            resolved.timeout_seconds = 60;
        }
        return std::make_unique<OllamaChatProvider>(std::move(resolved));
    }

    if (provider == "openai") {
        ChatConfig resolved = config;
        if (resolved.model.empty()) {
            resolved.model = "gpt-4o-mini";
        }
        if (resolved.base_url.empty()) {
            resolved.base_url = "https://api.openai.com/v1";
        }
        if (resolved.timeout_seconds < 1) {
            resolved.timeout_seconds = 60;
        }
        return std::make_unique<OpenAICompatibleChatProvider>(std::move(resolved));
    }

    throw ProductError("PROVIDER_CONFIG", "unknown chat provider: " + config.provider, 500);
}

}  // namespace hnswhy

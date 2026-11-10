#include "product/ollama_client.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"
#include "product/errors.h"

namespace hnswhy {
namespace {

using json = nlohmann::json;

std::vector<float> parse_vector(const json& value, const std::string& context) {
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

json parse_json(const HttpResult& response, const std::string& context) {
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

}  // namespace

OllamaClient::OllamaClient(std::string base_url, int timeout_seconds)
    : endpoint_(parse_endpoint(base_url)), timeout_seconds_(timeout_seconds) {
    if (timeout_seconds_ < 1) {
        throw ProductError("PROVIDER_CONFIG", "Ollama timeout must be positive", 500);
    }
}

std::vector<std::vector<float>> OllamaClient::embed(
    const std::string& model, const std::vector<std::string>& texts) const {
    if (model.empty()) {
        throw ProductError("PROVIDER_CONFIG", "Ollama embedding model is not configured", 500);
    }
    if (texts.empty()) {
        return {};
    }

    json body = {{"model", model}, {"input", texts}};
    const HttpResult response =
        http_post(endpoint_, "/api/embed", body.dump(), {}, timeout_seconds_);

    if (response.status == 404 || response.status == 405) {
        // Older Ollama versions only expose one-prompt embeddings.
        std::vector<std::vector<float>> result;
        result.reserve(texts.size());
        for (const std::string& text : texts) {
            json single = {{"model", model}, {"prompt", text}};
            const json parsed =
                parse_json(http_post(endpoint_, "/api/embeddings", single.dump(), {},
                                     timeout_seconds_),
                           "Ollama /api/embeddings");
            if (!parsed.contains("embedding")) {
                throw ProductError("PROVIDER_ERROR",
                                   "Ollama /api/embeddings response has no embedding", 502);
            }
            result.push_back(parse_vector(parsed["embedding"], "Ollama"));
        }
        return result;
    }

    const json parsed = parse_json(response, "Ollama /api/embed");
    if (!parsed.contains("embeddings") || !parsed["embeddings"].is_array()) {
        throw ProductError("PROVIDER_ERROR", "Ollama /api/embed response has no embeddings",
                           502);
    }
    const json& embeddings = parsed["embeddings"];
    if (embeddings.size() != texts.size()) {
        throw ProductError("PROVIDER_ERROR",
                           "Ollama returned a different number of embeddings than requested",
                           502);
    }

    std::vector<std::vector<float>> result;
    result.reserve(texts.size());
    for (const json& embedding : embeddings) {
        result.push_back(parse_vector(embedding, "Ollama"));
    }
    return result;
}

std::string OllamaClient::generate(const std::string& model,
                                   const std::string& prompt) const {
    if (model.empty()) {
        throw ProductError("PROVIDER_CONFIG", "Ollama chat model is not configured", 500);
    }
    json body = {{"model", model},
                 {"prompt", prompt},
                 {"stream", false},
                 {"options", {{"temperature", 0.2}}}};
    const json parsed =
        parse_json(http_post(endpoint_, "/api/generate", body.dump(), {}, timeout_seconds_),
                   "Ollama /api/generate");
    if (!parsed.contains("response") || !parsed["response"].is_string()) {
        throw ProductError("PROVIDER_ERROR", "Ollama /api/generate response has no text", 502);
    }
    const std::string answer = parsed["response"].get<std::string>();
    if (answer.empty()) {
        throw ProductError("PROVIDER_ERROR", "Ollama returned an empty answer", 502);
    }
    return answer;
}

bool OllamaClient::reachable() const {
    try {
        const HttpResult response = http_get(endpoint_, "/api/tags", {}, 2);
        return response.status >= 200 && response.status < 300;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace hnswhy

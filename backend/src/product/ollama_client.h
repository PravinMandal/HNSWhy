#pragma once

#include <string>
#include <vector>

#include "product/http.h"

namespace hnswhy {

// Minimal Ollama client. The embedding call uses the current /api/embed
// endpoint and falls back to the older one-prompt /api/embeddings endpoint.
class OllamaClient {
public:
    OllamaClient(std::string base_url, int timeout_seconds);

    std::vector<std::vector<float>> embed(const std::string& model,
                                          const std::vector<std::string>& texts) const;
    std::string generate(const std::string& model, const std::string& prompt) const;
    bool reachable() const;

private:
    HttpEndpoint endpoint_;
    int timeout_seconds_;
};

}  // namespace hnswhy

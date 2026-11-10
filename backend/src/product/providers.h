#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace hnswhy {

struct EmbeddingConfig {
    std::string provider = "hash";
    std::string model;
    std::string base_url;
    std::string api_key;
    std::size_t dim = 256;
    int timeout_seconds = 60;
};

struct ChatConfig {
    std::string provider = "extractive";
    std::string model;
    std::string base_url;
    std::string api_key;
    int timeout_seconds = 60;
};

class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    virtual std::string name() const = 0;
    virtual std::string model() const = 0;
    virtual std::size_t dim() const = 0;
    virtual std::vector<std::vector<float>> embed(
        const std::vector<std::string>& texts) const = 0;
    virtual bool available() const = 0;
};

struct ChatSource {
    std::string title;
    std::string text;
};

struct ChatRequest {
    std::string question;
    std::vector<ChatSource> sources;
    // RagService builds these once so HTTP providers receive the same grounded
    // instructions. The extractive provider uses question/sources directly.
    std::string system_prompt;
    std::string user_prompt;
};

class ChatProvider {
public:
    virtual ~ChatProvider() = default;

    virtual std::string name() const = 0;
    virtual std::string model() const = 0;
    virtual std::string answer(const ChatRequest& request) const = 0;
    virtual bool available() const = 0;
};

class HashEmbeddingProvider final : public EmbeddingProvider {
public:
    explicit HashEmbeddingProvider(std::size_t dim = 256,
                                   std::string model = "local-hash-v1");

    std::string name() const override { return "hash"; }
    std::string model() const override { return model_; }
    std::size_t dim() const override { return dim_; }
    std::vector<std::vector<float>> embed(
        const std::vector<std::string>& texts) const override;
    bool available() const override { return true; }

private:
    std::size_t dim_;
    std::string model_;
};

class ExtractiveChatProvider final : public ChatProvider {
public:
    explicit ExtractiveChatProvider(std::string model = "local-extractive-v1");

    std::string name() const override { return "extractive"; }
    std::string model() const override { return model_; }
    std::string answer(const ChatRequest& request) const override;
    bool available() const override { return true; }

private:
    std::string model_;
};

std::unique_ptr<EmbeddingProvider> create_embedding_provider(
    const EmbeddingConfig& config);
std::unique_ptr<ChatProvider> create_chat_provider(const ChatConfig& config);

}  // namespace hnswhy

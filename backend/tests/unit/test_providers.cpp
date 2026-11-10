#include "product/providers.h"

#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "framework/test_framework.h"
#include "httplib.h"
#include "json.hpp"
#include "product/errors.h"
#include "product/ollama_client.h"
#include "product/rag.h"
#include "product/text.h"

namespace {

using json = nlohmann::json;

class FakeServer {
public:
    FakeServer() {
        port_ = server_.bind_to_any_port("127.0.0.1");
        CHECK(port_ > 0);
    }

    ~FakeServer() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    FakeServer(const FakeServer&) = delete;
    FakeServer& operator=(const FakeServer&) = delete;

    httplib::Server& server() { return server_; }
    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

    void start() {
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }

private:
    httplib::Server server_;
    std::thread thread_;
    int port_ = 0;
};

std::vector<float> unit_vector(std::size_t dim, std::size_t hot) {
    std::vector<float> result(dim, 0.0f);
    result[hot % dim] = 1.0f;
    return result;
}

double norm(const std::vector<float>& values) {
    double sum = 0.0;
    for (const float value : values) {
        sum += static_cast<double>(value) * value;
    }
    return std::sqrt(sum);
}

}  // namespace

HNSWHY_TEST(text_tokenize_and_sentence_split) {
    const auto tokens = hnswhy::tokenize_lower("HNSW, Graph-Search! 42 café");
    CHECK_EQ(tokens.size(), std::size_t{5});
    CHECK_EQ(tokens[0], std::string("hnsw"));
    CHECK_EQ(tokens[1], std::string("graph"));
    CHECK_EQ(tokens[2], std::string("search"));
    CHECK_EQ(tokens[3], std::string("42"));

    const auto sentences = hnswhy::split_sentences("First sentence. Second one! Third one?");
    CHECK_EQ(sentences.size(), std::size_t{3});
    CHECK_EQ(sentences[1], std::string("Second one!"));
}

HNSWHY_TEST(hash_embedding_is_deterministic_and_normalized) {
    hnswhy::HashEmbeddingProvider provider(256);
    const auto first = provider.embed({"HNSW graph search for vector databases"});
    const auto second = provider.embed({"HNSW graph search for vector databases"});
    CHECK_EQ(first.size(), std::size_t{1});
    CHECK_EQ(first[0].size(), std::size_t{256});
    CHECK(first[0] == second[0]);
    CHECK_NEAR(norm(first[0]), 1.0, 1e-5);

    const auto unrelated = provider.embed({"banana smoothie recipe"});
    CHECK(first[0] != unrelated[0]);
}

HNSWHY_TEST(hash_embedding_ranks_related_text_closer) {
    hnswhy::HashEmbeddingProvider provider(256);
    const auto query = provider.embed({"how does efSearch affect recall"})[0];
    const auto relevant =
        provider.embed({"efSearch controls the candidate list size and recall during search"})[0];
    const auto unrelated = provider.embed({"chocolate cake baking instructions"})[0];

    auto distance = [](const std::vector<float>& a, const std::vector<float>& b) {
        double sum = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double diff = static_cast<double>(a[i]) - b[i];
            sum += diff * diff;
        }
        return sum;
    };
    CHECK(distance(query, relevant) < distance(query, unrelated));
}

HNSWHY_TEST(extractive_answer_cites_and_refuses) {
    hnswhy::ExtractiveChatProvider provider;
    hnswhy::ChatRequest request;
    request.question = "What does efSearch control?";
    request.sources = {
        {"HNSW", "## Search\n\nefSearch controls the size of the candidate list during a query."},
        {"Other", "The graph is built once and queried many times."},
    };
    const std::string answer = provider.answer(request);
    CHECK(answer.find("efSearch") != std::string::npos);
    CHECK(answer.find("[1]") != std::string::npos);
    CHECK(answer.find("## Search") == std::string::npos);

    request.question = "What is the capital of France?";
    CHECK_EQ(provider.answer(request), std::string(hnswhy::kNoAnswer));
}

HNSWHY_TEST(provider_factory_defaults_and_config_errors) {
    hnswhy::EmbeddingConfig hash;
    hash.provider = "hash";
    auto embedder = hnswhy::create_embedding_provider(hash);
    CHECK_EQ(embedder->name(), std::string("hash"));
    CHECK_EQ(embedder->model(), std::string("local-hash-v1"));
    CHECK_EQ(embedder->dim(), std::size_t{256});

    hnswhy::ChatConfig chat;
    auto chat_provider = hnswhy::create_chat_provider(chat);
    CHECK_EQ(chat_provider->name(), std::string("extractive"));

    hnswhy::EmbeddingConfig openai;
    openai.provider = "openai";
    CHECK_THROWS_AS(hnswhy::create_embedding_provider(openai), hnswhy::ProductError);

    hnswhy::EmbeddingConfig unknown;
    unknown.provider = "does-not-exist";
    CHECK_THROWS_AS(hnswhy::create_embedding_provider(unknown), hnswhy::ProductError);
}

HNSWHY_TEST(ollama_client_speaks_embed_and_generate) {
    FakeServer fake;
    fake.server().Post("/api/embed", [](const httplib::Request& request,
                                        httplib::Response& response) {
        const json body = json::parse(request.body);
        const auto inputs = body["input"].get<std::vector<std::string>>();
        json embeddings = json::array();
        for (const std::string& input : inputs) {
            embeddings.push_back(unit_vector(4, input.size()));
        }
        response.set_content(json{{"embeddings", embeddings}}.dump(), "application/json");
    });
    fake.server().Post("/api/generate", [](const httplib::Request&,
                                           httplib::Response& response) {
        response.set_content(json{{"response", "grounded answer [1]"}}.dump(),
                             "application/json");
    });
    fake.server().Get("/api/tags", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("{}", "application/json");
    });
    fake.start();

    hnswhy::EmbeddingConfig config;
    config.provider = "ollama";
    config.model = "nomic-embed-text";
    config.base_url = fake.base_url();
    auto provider = hnswhy::create_embedding_provider(config);
    CHECK(provider->available());
    const auto embeddings = provider->embed({"alpha", "beta"});
    CHECK_EQ(embeddings.size(), std::size_t{2});
    CHECK_EQ(embeddings[0].size(), std::size_t{4});
    CHECK_NEAR(norm(embeddings[0]), 1.0, 1e-5);

    hnswhy::ChatConfig chat;
    chat.provider = "ollama";
    chat.model = "llama3.2";
    chat.base_url = fake.base_url();
    auto chat_provider = hnswhy::create_chat_provider(chat);
    hnswhy::ChatRequest request;
    request.question = "q";
    request.system_prompt = "system";
    request.user_prompt = "user";
    CHECK_EQ(chat_provider->answer(request), std::string("grounded answer [1]"));
}

HNSWHY_TEST(ollama_client_falls_back_to_single_embeddings) {
    FakeServer fake;
    fake.server().Post("/api/embed", [](const httplib::Request&,
                                        httplib::Response& response) {
        response.status = 404;
        response.set_content("{}", "application/json");
    });
    fake.server().Post("/api/embeddings", [](const httplib::Request&,
                                             httplib::Response& response) {
        response.set_content(json{{"embedding", unit_vector(3, 1)}}.dump(),
                             "application/json");
    });
    fake.start();

    hnswhy::OllamaClient client(fake.base_url(), 5);
    const auto embeddings = client.embed("legacy", {"hello"});
    CHECK_EQ(embeddings.size(), std::size_t{1});
    CHECK_EQ(embeddings[0].size(), std::size_t{3});
}

HNSWHY_TEST(openai_compatible_client_rejects_bad_embedding_indices) {
    FakeServer fake;
    fake.server().Post("/v1/embeddings", [](const httplib::Request&,
                                            httplib::Response& response) {
        const json data = json::array({
            {{"index", 0}, {"embedding", unit_vector(3, 0)}},
            {{"index", 0}, {"embedding", unit_vector(3, 1)}},
        });
        response.set_content(json{{"data", data}}.dump(), "application/json");
    });
    fake.start();

    hnswhy::EmbeddingConfig config;
    config.provider = "openai";
    config.model = "text-embedding-3-small";
    config.base_url = fake.base_url() + "/v1";
    config.api_key = "test-key";
    auto provider = hnswhy::create_embedding_provider(config);
    CHECK_THROWS_AS(provider->embed({"first", "second"}), hnswhy::ProductError);
}

HNSWHY_TEST(openai_compatible_client_embeds_and_chats) {
    FakeServer fake;
    fake.server().Post("/v1/embeddings", [](const httplib::Request& request,
                                            httplib::Response& response) {
        const json body = json::parse(request.body);
        const auto inputs = body["input"].get<std::vector<std::string>>();
        json data = json::array();
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            data.push_back({{"index", static_cast<int>(inputs.size() - 1 - i)},
                            {"embedding", unit_vector(5, i + 1)}});
        }
        response.set_content(json{{"data", data}}.dump(), "application/json");
    });
    fake.server().Post("/v1/chat/completions", [](const httplib::Request&,
                                                   httplib::Response& response) {
        response.set_content(
            json{{"choices", json::array({{{"message", {{"content", "cited [1]"}}}}})}}
                .dump(),
            "application/json");
    });
    fake.server().Get("/v1/models", [](const httplib::Request&,
                                       httplib::Response& response) {
        response.set_content("{}", "application/json");
    });
    fake.start();

    hnswhy::EmbeddingConfig config;
    config.provider = "openai";
    config.model = "text-embedding-3-small";
    config.base_url = fake.base_url() + "/v1";
    config.api_key = "test-key";
    auto provider = hnswhy::create_embedding_provider(config);
    CHECK(provider->available());
    const auto embeddings = provider->embed({"first", "second"});
    CHECK_EQ(embeddings.size(), std::size_t{2});
    CHECK_NEAR(embeddings[0][2], 1.0f, 1e-6);
    CHECK_NEAR(embeddings[1][1], 1.0f, 1e-6);

    hnswhy::ChatConfig chat;
    chat.provider = "openai";
    chat.model = "gpt-4o-mini";
    chat.base_url = fake.base_url() + "/v1";
    chat.api_key = "test-key";
    auto chat_provider = hnswhy::create_chat_provider(chat);
    hnswhy::ChatRequest request;
    request.system_prompt = "system";
    request.user_prompt = "user";
    CHECK_EQ(chat_provider->answer(request), std::string("cited [1]"));
}

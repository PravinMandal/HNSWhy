#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include <unistd.h>

#include "framework/test_framework.h"
#include "product/config.h"
#include "product/errors.h"

namespace {

// load_config reads the real environment. A developer's shell may legitimately
// set HNSWHY_* or OPENAI_API_KEY, so every config test starts from a clean
// slate and restores the original values on destruction.
class ScopedCleanEnvironment {
public:
    ScopedCleanEnvironment() {
        for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
            const std::string variable(*entry);
            const std::size_t equals = variable.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            const std::string name = variable.substr(0, equals);
            if (is_relevant(name)) {
                saved_.push_back(variable);
                unsetenv(name.c_str());
            }
        }
    }

    ~ScopedCleanEnvironment() {
        for (const std::string& variable : saved_) {
            const std::size_t equals = variable.find('=');
            const std::string name = variable.substr(0, equals);
            setenv(name.c_str(), variable.substr(equals + 1).c_str(), 1);
        }
    }

    ScopedCleanEnvironment(const ScopedCleanEnvironment&) = delete;
    ScopedCleanEnvironment& operator=(const ScopedCleanEnvironment&) = delete;

private:
    static bool is_relevant(const std::string& name) {
        return name.rfind("HNSWHY_", 0) == 0 || name == "OPENAI_API_KEY";
    }

    std::vector<std::string> saved_;
};

hnswhy::AppConfig load_with(std::initializer_list<const char*> arguments) {
    std::vector<std::string> storage{"hnswhy_server"};
    storage.reserve(arguments.size() + 1);
    for (const char* argument : arguments) {
        storage.emplace_back(argument);
    }

    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (std::string& argument : storage) {
        argv.push_back(argument.data());
    }
    return hnswhy::load_config(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

HNSWHY_TEST(config_derives_m0_from_m_when_m0_is_not_explicit) {
    const ScopedCleanEnvironment environment;
    const hnswhy::AppConfig config = load_with({"--M", "64"});
    CHECK_EQ(config.hnsw.M, std::size_t{64});
    CHECK_EQ(config.hnsw.M0, std::size_t{128});
}

HNSWHY_TEST(config_keeps_explicit_m0) {
    const ScopedCleanEnvironment environment;
    const hnswhy::AppConfig config = load_with({"--M", "64", "--M0", "96"});
    CHECK_EQ(config.hnsw.M, std::size_t{64});
    CHECK_EQ(config.hnsw.M0, std::size_t{96});
}

HNSWHY_TEST(config_rejects_m0_smaller_than_m) {
    const ScopedCleanEnvironment environment;
    CHECK_THROWS_AS(load_with({"--M", "32", "--M0", "16"}), hnswhy::ProductError);
}

HNSWHY_TEST(config_reads_environment_overrides) {
    const ScopedCleanEnvironment environment;
    setenv("HNSWHY_HNSW_M", "24", 1);
    setenv("HNSWHY_CHUNK_WORDS", "400", 1);
    setenv("HNSWHY_CHUNK_OVERLAP", "25", 1);
    const hnswhy::AppConfig config = load_with({});
    CHECK_EQ(config.hnsw.M, std::size_t{24});
    CHECK_EQ(config.hnsw.M0, std::size_t{48});
    CHECK_EQ(config.chunk_target_words, std::size_t{400});
    CHECK_EQ(config.chunk_overlap_words, std::size_t{25});
}

HNSWHY_TEST(config_uses_openai_key_only_for_the_default_endpoint) {
    const ScopedCleanEnvironment environment;
    setenv("OPENAI_API_KEY", "sk-test-value", 1);

    const hnswhy::AppConfig openai = load_with({"--embedding-provider", "openai"});
    CHECK_EQ(openai.embedding.api_key, std::string("sk-test-value"));

    const hnswhy::AppConfig custom = load_with(
        {"--embedding-provider", "openai", "--embedding-base-url", "http://127.0.0.1:1234/v1"});
    CHECK(custom.embedding.api_key.empty());

    const hnswhy::AppConfig chat_custom = load_with(
        {"--chat-provider", "openai", "--chat-base-url", "https://example.test/v1"});
    CHECK(chat_custom.chat.api_key.empty());

    // An explicit key always wins, even on a custom endpoint.
    setenv("HNSWHY_EMBEDDING_API_KEY", "custom-key", 1);
    const hnswhy::AppConfig explicit_key = load_with(
        {"--embedding-provider", "openai", "--embedding-base-url", "https://example.test/v1"});
    CHECK_EQ(explicit_key.embedding.api_key, std::string("custom-key"));
}

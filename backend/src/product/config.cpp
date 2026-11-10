#include "product/config.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "product/errors.h"

namespace hnswhy {
namespace {

std::string trim(std::string value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

const char* env_value(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return nullptr;
    }
    return value;
}

long parse_long(std::string_view name, std::string_view value, long minimum, long maximum) {
    std::string text(value);
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed < minimum ||
        parsed > maximum) {
        throw ProductError("CONFIG_ERROR",
                           std::string(name) + " must be between " +
                               std::to_string(minimum) + " and " +
                               std::to_string(maximum),
                           500);
    }
    return parsed;
}

long env_long(const char* name, long fallback, long minimum, long maximum) {
    const char* value = env_value(name);
    return value == nullptr ? fallback : parse_long(name, value, minimum, maximum);
}

bool parse_bool(std::string_view name, std::string_view value) {
    const std::string normalized = lower(trim(std::string(value)));
    if (normalized == "1" || normalized == "true" || normalized == "yes" ||
        normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" ||
        normalized == "off") {
        return false;
    }
    throw ProductError("CONFIG_ERROR",
                       std::string(name) + " must be one of 1/0, true/false, yes/no, on/off",
                       500);
}

bool env_bool(const char* name, bool fallback) {
    const char* value = env_value(name);
    return value == nullptr ? fallback : parse_bool(name, value);
}

std::string env_string(const char* name, std::string fallback = {}) {
    const char* value = env_value(name);
    return value == nullptr ? std::move(fallback) : std::string(value);
}

void apply_environment(AppConfig& config) {
    config.db_path = env_string("HNSWHY_DB", config.db_path.string());
    config.bench_report = env_string("HNSWHY_BENCH_REPORT", config.bench_report.string());
    config.static_dir = env_string("HNSWHY_STATIC_DIR", config.static_dir.string());
    config.samples_dir = env_string("HNSWHY_SAMPLES_DIR", config.samples_dir.string());
    config.host = env_string("HNSWHY_HOST", config.host);
    config.port = static_cast<int>(env_long("HNSWHY_PORT", config.port, 1, 65535));
    config.load_sample = env_bool("HNSWHY_LOAD_SAMPLE", config.load_sample);
    config.reembed = env_bool("HNSWHY_REEMBED", config.reembed);

    config.embedding.provider = env_string("HNSWHY_EMBEDDING_PROVIDER", config.embedding.provider);
    config.embedding.model = env_string("HNSWHY_EMBEDDING_MODEL", config.embedding.model);
    config.embedding.base_url = env_string("HNSWHY_EMBEDDING_BASE_URL", config.embedding.base_url);
    config.embedding.api_key = env_string("HNSWHY_EMBEDDING_API_KEY", config.embedding.api_key);
    config.embedding.timeout_seconds = static_cast<int>(
        env_long("HNSWHY_EMBEDDING_TIMEOUT_SECONDS", config.embedding.timeout_seconds, 1, 600));
    config.chat.provider = env_string("HNSWHY_CHAT_PROVIDER", config.chat.provider);
    config.chat.model = env_string("HNSWHY_CHAT_MODEL", config.chat.model);
    config.chat.base_url = env_string("HNSWHY_CHAT_BASE_URL", config.chat.base_url);
    config.chat.api_key = env_string("HNSWHY_CHAT_API_KEY", config.chat.api_key);
    config.chat.timeout_seconds = static_cast<int>(
        env_long("HNSWHY_CHAT_TIMEOUT_SECONDS", config.chat.timeout_seconds, 1, 600));
    if (env_value("HNSWHY_EMBEDDING_DIM") != nullptr) {
        config.embedding.dim = static_cast<std::size_t>(
            env_long("HNSWHY_EMBEDDING_DIM", config.embedding.dim, 0, 65536));
    }

    if (config.embedding.api_key.empty()) {
        config.embedding.api_key = env_string("OPENAI_API_KEY");
    }
    if (config.chat.api_key.empty()) {
        config.chat.api_key = env_string("OPENAI_API_KEY");
    }

    config.hnsw.M = static_cast<std::size_t>(env_long("HNSWHY_HNSW_M", 16, 4, 1024));
    config.hnsw.M0 = static_cast<std::size_t>(env_long("HNSWHY_HNSW_M0", 32, 4, 65535));
    config.hnsw.efConstruction =
        static_cast<std::size_t>(env_long("HNSWHY_HNSW_EF_CONSTRUCTION", 200, 1, 10000));
    config.hnsw.seed = static_cast<std::uint64_t>(
        env_long("HNSWHY_HNSW_SEED", static_cast<long>(config.hnsw.seed), 0, 1000000000));
    config.query_ef = static_cast<std::size_t>(env_long("HNSWHY_QUERY_EF", 50, 1, 10000));
    config.default_k = static_cast<std::size_t>(env_long("HNSWHY_DEFAULT_K", 5, 1, 20));
    config.chunk_target_words =
        static_cast<std::size_t>(env_long("HNSWHY_CHUNK_WORDS", 350, 20, 4000));
    config.chunk_overlap_words =
        static_cast<std::size_t>(env_long("HNSWHY_CHUNK_OVERLAP", 50, 0, 2000));
}

std::string take_value(int argc, char** argv, int& index, std::string_view flag) {
    if (index + 1 >= argc) {
        throw ProductError("CONFIG_ERROR", std::string(flag) + " requires a value", 500);
    }
    ++index;
    return argv[index];
}

void apply_command_line(AppConfig& config, int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--help" || flag == "-h") {
            config.show_help = true;
        } else if (flag == "--db") {
            config.db_path = take_value(argc, argv, i, flag);
        } else if (flag == "--port") {
            config.port = static_cast<int>(
                parse_long(flag, take_value(argc, argv, i, flag), 1, 65535));
        } else if (flag == "--host") {
            config.host = take_value(argc, argv, i, flag);
        } else if (flag == "--bench-report") {
            config.bench_report = take_value(argc, argv, i, flag);
        } else if (flag == "--static-dir") {
            config.static_dir = take_value(argc, argv, i, flag);
        } else if (flag == "--samples-dir") {
            config.samples_dir = take_value(argc, argv, i, flag);
        } else if (flag == "--no-sample") {
            config.load_sample = false;
        } else if (flag == "--reembed") {
            config.reembed = true;
        } else if (flag == "--embedding-provider") {
            config.embedding.provider = take_value(argc, argv, i, flag);
        } else if (flag == "--embedding-model") {
            config.embedding.model = take_value(argc, argv, i, flag);
        } else if (flag == "--embedding-base-url") {
            config.embedding.base_url = take_value(argc, argv, i, flag);
        } else if (flag == "--embedding-dim") {
            config.embedding.dim = static_cast<std::size_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 0, 65536));
        } else if (flag == "--chat-provider") {
            config.chat.provider = take_value(argc, argv, i, flag);
        } else if (flag == "--chat-model") {
            config.chat.model = take_value(argc, argv, i, flag);
        } else if (flag == "--chat-base-url") {
            config.chat.base_url = take_value(argc, argv, i, flag);
        } else if (flag == "--M") {
            config.hnsw.M = static_cast<std::size_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 4, 1024));
        } else if (flag == "--M0") {
            config.hnsw.M0 = static_cast<std::size_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 4, 65535));
        } else if (flag == "--ef-construction") {
            config.hnsw.efConstruction = static_cast<std::size_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 1, 10000));
        } else if (flag == "--ef-search") {
            config.query_ef = static_cast<std::size_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 1, 10000));
        } else if (flag == "--seed") {
            config.hnsw.seed = static_cast<std::uint64_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 0, 1000000000));
        } else if (flag == "--chunk-words") {
            config.chunk_target_words = static_cast<std::size_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 20, 4000));
        } else if (flag == "--chunk-overlap") {
            config.chunk_overlap_words = static_cast<std::size_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 0, 2000));
        } else if (flag == "--default-k") {
            config.default_k = static_cast<std::size_t>(
                parse_long(flag, take_value(argc, argv, i, flag), 1, 20));
        } else {
            throw ProductError("CONFIG_ERROR", "unknown option: " + flag, 500);
        }
    }
}

}  // namespace

AppConfig load_config(int argc, char** argv) {
    AppConfig config;
    apply_environment(config);
    apply_command_line(config, argc, argv);

    bool cli_dim_explicit = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--embedding-dim") {
            cli_dim_explicit = true;
            break;
        }
    }
    const bool dim_explicit = env_value("HNSWHY_EMBEDDING_DIM") != nullptr || cli_dim_explicit;
    if (!dim_explicit) {
        const std::string provider = lower(trim(config.embedding.provider));
        config.embedding.dim = provider.empty() || provider == "hash" ? 256 : 0;
    }

    if (config.port < 1 || config.port > 65535) {
        throw ProductError("CONFIG_ERROR", "port must be between 1 and 65535", 500);
    }
    if (config.chunk_overlap_words >= config.chunk_target_words) {
        throw ProductError("CONFIG_ERROR", "chunk overlap must be smaller than chunk words",
                           500);
    }
    return config;
}

std::string usage() {
    return R"(HNSWhy server

Usage: hnswhy_server [options]

  --db PATH                 SQLite database (default data/hnswhy.db)
  --host HOST               bind host (default 127.0.0.1)
  --port N                  bind port (default 8080)
  --bench-report PATH       benchmark JSON (default docs/results/bench.json)
  --static-dir PATH         frontend build (default frontend/dist)
  --samples-dir PATH        sample knowledge base (default samples)
  --no-sample               do not seed the sample knowledge base
  --reembed                 rebuild every embedding with the active provider
  --embedding-provider P    hash | ollama | openai
  --embedding-model M       embedding model name
  --embedding-base-url URL  OpenAI-compatible or Ollama base URL
  --embedding-dim N         expected embedding dimension (0 = infer)
  --chat-provider P         extractive | ollama | openai
  --chat-model M            chat model name
  --chat-base-url URL       OpenAI-compatible or Ollama base URL
  --M N                     HNSW M (default 16)
  --M0 N                    HNSW layer-0 degree (default 32)
  --ef-construction N       HNSW build beam (default 200)
  --ef-search N             HNSW query beam (default 50)
  --seed N                  HNSW level seed (default 42)
  --chunk-words N           chunk target words (default 350)
  --chunk-overlap N         chunk overlap words (default 50)
  --default-k N             default retrieval count (default 5)

API keys are read from the environment, never from command-line flags.
See .env.example for the full list.
)";
}

}  // namespace hnswhy

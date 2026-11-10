#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "engine/hnsw.h"
#include "product/providers.h"

namespace hnswhy {

struct AppConfig {
    std::filesystem::path db_path = "data/hnswhy.db";
    std::filesystem::path bench_report = "docs/results/bench.json";
    std::filesystem::path static_dir = "frontend/dist";
    std::filesystem::path samples_dir = "samples";
    std::string host = "127.0.0.1";
    int port = 8080;

    bool load_sample = true;
    bool reembed = false;
    bool show_help = false;

    EmbeddingConfig embedding;
    ChatConfig chat;
    HNSWParams hnsw;

    std::size_t query_ef = 50;
    std::size_t chunk_target_words = 350;
    std::size_t chunk_overlap_words = 50;
    std::size_t default_k = 5;
};

// Environment first, command line second. API keys are intentionally
// environment-only so they never appear in a process listing.
AppConfig load_config(int argc, char** argv);
std::string usage();

}  // namespace hnswhy

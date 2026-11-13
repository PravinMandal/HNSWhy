#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

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
    // Extra Host header names accepted by the HTTP server. Loopback names and
    // the concrete bind host are always accepted; this list is for reverse
    // proxies and LAN names when binding to a wildcard address.
    std::vector<std::string> allowed_hosts;

    bool load_sample = true;
    bool reembed = false;
    bool show_help = false;

    EmbeddingConfig embedding;
    ChatConfig chat;
    HNSWParams hnsw;

    std::size_t query_ef = 50;
    // Cosine-similarity floor for retrieved chunks. Below this, the chunks are
    // treated as unrelated and the chat provider answers from general
    // knowledge instead. 0 disables the filter; 0.40 suits nomic-embed-text,
    // while lexical hash embeddings need a much lower floor.
    double min_source_similarity = 0.0;
    std::size_t chunk_target_words = 350;
    std::size_t chunk_overlap_words = 50;
    std::size_t default_k = 5;
};

// Environment first, command line second. API keys are intentionally
// environment-only so they never appear in a process listing.
AppConfig load_config(int argc, char** argv);
std::string usage();

}  // namespace hnswhy

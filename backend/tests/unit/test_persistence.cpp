#include "product/app.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "framework/temp_path.h"
#include "framework/test_framework.h"
#include "product/errors.h"

#include <sqlite3.h>

namespace {

using hnswhytest::TempDir;
using hnswhytest::TempPath;

constexpr const char* kDocumentText =
    "HNSW builds a navigable small world graph for approximate nearest neighbor search. "
    "The graph has multiple layers and each layer is a proximity graph. "
    "Search starts at the top layer and descends greedily toward the query. "
    "At layer zero the search explores a candidate list controlled by efSearch. "
    "efSearch controls the size of the candidate list during a query. "
    "Larger efSearch usually improves recall but increases latency. "
    "SQLite stores documents chunks and embeddings as the source of truth. "
    "The vector index is rebuilt from SQLite at startup. "
    "New documents are inserted incrementally without rebuilding the whole index. "
    "Deleting a document marks its vector slots as tombstones. "
    "Tombstones remain traversable but are filtered from search results. "
    "The embedding provider and model are recorded in metadata.";

hnswhy::AppConfig test_config(const TempPath& db, const TempDir& dir) {
    hnswhy::AppConfig config;
    config.db_path = db.path();
    config.bench_report = dir.path() / "bench.json";
    config.static_dir = dir.path() / "web";
    config.samples_dir = dir.path() / "samples";
    config.load_sample = false;
    config.embedding.provider = "hash";
    config.embedding.dim = 256;
    config.chat.provider = "extractive";
    config.chunk_target_words = 40;
    config.chunk_overlap_words = 10;
    config.query_ef = 32;
    return config;
}

void check_ready_application(hnswhy::Application& app, std::size_t expected_chunks) {
    const hnswhy::StatusView status = app.status();
    CHECK_EQ(status.store.documents, std::size_t{1});
    CHECK_EQ(status.store.chunks, expected_chunks);
    CHECK_EQ(status.store.embeddings, expected_chunks);
    CHECK_EQ(status.index_live, expected_chunks);
    CHECK_EQ(status.index_slots, expected_chunks);
    CHECK_EQ(status.index_tombstones, std::size_t{0});
    CHECK_EQ(status.dim, std::size_t{256});
    CHECK_EQ(status.embedding_provider, std::string("hash"));
    CHECK_EQ(status.embedding_model, std::string("local-hash-v1"));
    CHECK_EQ(status.chat_provider, std::string("extractive"));
    CHECK_EQ(status.query_ef, std::size_t{32});
    CHECK(!status.sample_loaded);
}

void exec_sql(const TempPath& db, const std::string& sql) {
    sqlite3* handle = nullptr;
    CHECK_EQ(sqlite3_open(db.path().string().c_str(), &handle), SQLITE_OK);
    char* error = nullptr;
    const int status = sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &error);
    if (error != nullptr) {
        sqlite3_free(error);
    }
    sqlite3_close(handle);
    CHECK_EQ(status, SQLITE_OK);
}

}  // namespace

HNSWHY_TEST(application_uploads_asks_maps_and_deletes) {
    TempPath db(".db");
    TempDir dir;
    hnswhy::Application app(test_config(db, dir));

    const hnswhy::DocumentSummary added =
        app.add_document("Engine notes", "notes.md", kDocumentText);
    CHECK(added.id > 0);
    CHECK(added.chunk_count >= 2);
    CHECK_EQ(added.title, std::string("Engine notes"));
    CHECK_EQ(added.source, std::string("notes.md"));

    const auto documents = app.list_documents();
    CHECK_EQ(documents.size(), std::size_t{1});
    CHECK_EQ(documents[0].id, added.id);

    const auto detail = app.get_document(added.id);
    CHECK(detail.has_value());
    CHECK_EQ(detail->summary.chunk_count, added.chunk_count);
    CHECK_EQ(detail->chunks.size(), added.chunk_count);
    CHECK_EQ(detail->text, std::string(kDocumentText));
    CHECK_EQ(detail->chunks[0].ordinal, 0);

    check_ready_application(app, added.chunk_count);

    const hnswhy::HealthView health = app.health();
    CHECK(health.ok);
    CHECK_EQ(health.database, std::string("ok"));
    CHECK_EQ(health.store.documents, std::size_t{1});
    CHECK_EQ(health.embedding.provider, std::string("hash"));
    CHECK(health.embedding.available);
    CHECK(health.chat.available);

    const hnswhy::MapView map = app.map(100);
    CHECK_EQ(map.total_chunks, added.chunk_count);
    CHECK_EQ(map.points.size(), added.chunk_count);
    for (const hnswhy::MapPoint& point : map.points) {
        CHECK_EQ(point.document_id, added.id);
        CHECK_EQ(point.title, std::string("Engine notes"));
        CHECK_EQ(point.vector.size(), std::size_t{256});
        CHECK(!point.preview.empty());
    }

    const hnswhy::RagAnswer answer = app.ask("What does efSearch control?", 3);
    CHECK(!answer.answer.empty());
    CHECK(answer.answer != std::string(hnswhy::kNoAnswer));
    CHECK(!answer.sources.empty());
    CHECK(answer.sources[0].document_id == added.id);
    CHECK_EQ(answer.embedding_provider, std::string("hash"));
    CHECK_EQ(answer.chat_provider, std::string("extractive"));
    CHECK(answer.timings.total_ms >= 0.0);

    CHECK(app.delete_document(added.id));
    CHECK(app.list_documents().empty());
    const hnswhy::StatusView after_delete = app.status();
    CHECK_EQ(after_delete.store.documents, std::size_t{0});
    CHECK_EQ(after_delete.store.chunks, std::size_t{0});
    CHECK_EQ(after_delete.index_live, std::size_t{0});
    CHECK(after_delete.index_slots >= added.chunk_count);

    const hnswhy::RagAnswer refusal = app.ask("What does efSearch control?", 3);
    CHECK_EQ(refusal.answer, std::string(hnswhy::kNoAnswer));
    CHECK(refusal.sources.empty());

    const hnswhy::DocumentSummary second =
        app.add_document("Fresh notes", "fresh.md", kDocumentText);
    CHECK(second.id > added.id);
    const hnswhy::StatusView after_add = app.status();
    CHECK_EQ(after_add.store.documents, std::size_t{1});
    CHECK_EQ(after_add.index_live, second.chunk_count);
    CHECK_EQ(after_add.index_slots, second.chunk_count);
}

HNSWHY_TEST(application_validates_uploads_and_lookups) {
    TempPath db(".db");
    TempDir dir;
    hnswhy::Application app(test_config(db, dir));

    CHECK_THROWS_AS(app.add_document("", "empty.txt", ""), hnswhy::ProductError);
    CHECK_THROWS_AS(app.add_document(std::string(201, 'a'), "source.txt", "text"),
                    hnswhy::ProductError);
    CHECK_THROWS_AS(app.map(0), hnswhy::ProductError);
    CHECK_THROWS_AS(app.map(2001), hnswhy::ProductError);
    CHECK(!app.delete_document(999));
    CHECK(!app.get_document(999).has_value());
    CHECK(app.list_documents().empty());
}

HNSWHY_TEST(application_survives_restart_and_rebuilds_index) {
    TempPath db(".db");
    TempDir dir;
    hnswhy::DocumentSummary added;
    {
        hnswhy::Application app(test_config(db, dir));
        added = app.add_document("Persistent notes", "persistent.md", kDocumentText);
        CHECK(added.id > 0);
    }

    hnswhy::Application app(test_config(db, dir));
    const auto documents = app.list_documents();
    CHECK_EQ(documents.size(), std::size_t{1});
    CHECK_EQ(documents[0].id, added.id);
    CHECK_EQ(documents[0].chunk_count, added.chunk_count);
    check_ready_application(app, added.chunk_count);

    const hnswhy::RagAnswer answer = app.ask("How does search descend the graph?", 2);
    CHECK(!answer.sources.empty());
    CHECK(answer.answer != std::string(hnswhy::kNoAnswer));

    CHECK(app.delete_document(added.id));
    CHECK(app.list_documents().empty());
    CHECK_EQ(app.status().index_live, std::size_t{0});
}

HNSWHY_TEST(application_reembeds_when_the_embedding_space_changes) {
    TempPath db(".db");
    TempDir dir;
    const hnswhy::DocumentSummary added = [&] {
        hnswhy::Application app(test_config(db, dir));
        return app.add_document("Reembed notes", "reembed.md", kDocumentText);
    }();

    hnswhy::AppConfig changed = test_config(db, dir);
    changed.embedding.dim = 128;
    bool mismatch = false;
    try {
        hnswhy::Application rejected(changed);
    } catch (const hnswhy::ProductError& error) {
        mismatch = true;
        CHECK_EQ(error.code(), std::string("EMBEDDING_MISMATCH"));
    }
    CHECK(mismatch);

    changed.reembed = true;
    hnswhy::Application app(changed);
    const auto documents = app.list_documents();
    CHECK_EQ(documents.size(), std::size_t{1});
    CHECK_EQ(documents[0].id, added.id);
    const hnswhy::StatusView status = app.status();
    CHECK_EQ(status.dim, std::size_t{128});
    CHECK_EQ(status.embedding_model, std::string("local-hash-v1"));
    CHECK_EQ(status.store.chunks, added.chunk_count);
    CHECK_EQ(status.index_live, added.chunk_count);
    CHECK(!app.ask("What does efSearch control?", 2).sources.empty());
}

HNSWHY_TEST(application_reembed_repairs_missing_rows_and_metadata) {
    TempPath db(".db");
    TempDir dir;
    const hnswhy::DocumentSummary added = [&] {
        hnswhy::Application app(test_config(db, dir));
        return app.add_document("Repair notes", "repair.md", kDocumentText);
    }();

    exec_sql(db, "DELETE FROM embeddings WHERE chunk_id = (SELECT MIN(id) FROM chunks)");
    exec_sql(db, "DELETE FROM meta WHERE key = 'embedding_dim'");

    hnswhy::AppConfig config = test_config(db, dir);
    config.reembed = true;
    hnswhy::Application app(config);
    const hnswhy::StatusView status = app.status();
    CHECK_EQ(status.store.documents, std::size_t{1});
    CHECK_EQ(status.store.chunks, added.chunk_count);
    CHECK_EQ(status.store.embeddings, added.chunk_count);
    CHECK_EQ(status.index_live, added.chunk_count);
    CHECK_EQ(status.dim, std::size_t{256});
    CHECK_EQ(app.list_documents()[0].id, added.id);
    CHECK(!app.ask("What does efSearch control?", 2).sources.empty());
}

HNSWHY_TEST(application_seeds_samples_once_on_a_fresh_database) {
    TempPath db(".db");
    TempDir dir;
    const std::filesystem::path samples = dir.path() / "samples";
    std::filesystem::create_directories(samples);
    {
        std::ofstream file(samples / "engine.md");
        file << "# Engine guide\n\n" << kDocumentText << '\n';
    }
    {
        std::ofstream file(samples / "rag.txt");
        file << "Retrieval augmented generation combines search with a language model.\n"
             << "The retriever finds relevant chunks and the model answers with citations.\n";
    }

    hnswhy::AppConfig config = test_config(db, dir);
    config.load_sample = true;
    const hnswhy::StatusView first_status = [&] {
        hnswhy::Application app(config);
        const auto documents = app.list_documents();
        CHECK_EQ(documents.size(), std::size_t{2});
        bool found_engine = false;
        bool found_rag = false;
        for (const hnswhy::DocumentSummary& document : documents) {
            found_engine = found_engine || document.title == "Engine guide";
            found_rag = found_rag || document.title == "rag";
        }
        CHECK(found_engine);
        CHECK(found_rag);
        CHECK(app.status().sample_loaded);
        return app.status();
    }();
    CHECK(first_status.store.documents == 2);
    CHECK(first_status.index_live == first_status.store.chunks);

    hnswhy::Application reopened(config);
    const auto documents = reopened.list_documents();
    CHECK_EQ(documents.size(), std::size_t{2});
    CHECK(reopened.status().sample_loaded);
}

HNSWHY_TEST(application_serves_a_benchmark_only_when_the_report_exists) {
    TempPath db(".db");
    TempDir dir;
    hnswhy::AppConfig config = test_config(db, dir);

    hnswhy::Application app(config);
    CHECK(!app.benchmark_json().has_value());
    CHECK(!app.status().benchmark_available);

    {
        std::ofstream report(config.bench_report);
        report << R"({"schema_version":1,"dataset":{"name":"sift100k"}})";
    }
    const auto report = app.benchmark_json();
    CHECK(report.has_value());
    CHECK(report->find("sift100k") != std::string::npos);
    CHECK(app.status().benchmark_available);

    config.bench_report = dir.path() / "missing.json";
    hnswhy::Application missing(config);
    CHECK(!missing.benchmark_json().has_value());
}

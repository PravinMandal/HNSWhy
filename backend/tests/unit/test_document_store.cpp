#include "product/document_store.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "framework/temp_path.h"
#include "framework/test_framework.h"
#include "product/errors.h"

#include <sqlite3.h>

namespace {

// Temporary files include a PID plus a per-process counter, so concurrent
// test binaries or a reused PID cannot collide on a stale database.
class TempDb {
public:
    explicit TempDb(const std::string& label) : path_(label + ".db") {}

    const std::filesystem::path& path() const { return path_.path(); }

private:
    hnswhytest::TempPath path_;
};

std::vector<float> embedding(float first, float second = 0.0f) {
    return {first, second, 0.5f, -0.25f};
}

hnswhy::NewChunk chunk(const std::string& text, float first) {
    return hnswhy::NewChunk{text, embedding(first)};
}

void exec_sql(const std::filesystem::path& path, const std::string& sql) {
    sqlite3* db = nullptr;
    CHECK_EQ(sqlite3_open(path.string().c_str(), &db), SQLITE_OK);
    char* error = nullptr;
    const int status = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
    if (error != nullptr) {
        sqlite3_free(error);
    }
    sqlite3_close(db);
    CHECK_EQ(status, SQLITE_OK);
}

}  // namespace

HNSWHY_TEST(document_store_insert_list_and_get) {
    TempDb file("insert");
    hnswhy::DocumentStore store(file.path());

    const hnswhy::InsertResult inserted = store.insert_document(
        "Graph search", "test.txt", "A document about graphs.",
        {chunk("first chunk", 1.0f), chunk("second chunk", 2.0f)});
    CHECK(inserted.document_id > 0);
    CHECK_EQ(inserted.chunk_ids.size(), std::size_t{2});

    const auto documents = store.list_documents();
    CHECK_EQ(documents.size(), std::size_t{1});
    CHECK_EQ(documents[0].id, inserted.document_id);
    CHECK_EQ(documents[0].title, std::string("Graph search"));
    CHECK_EQ(documents[0].source, std::string("test.txt"));
    CHECK_EQ(documents[0].chunk_count, std::size_t{2});
    CHECK_EQ(documents[0].char_count, std::size_t{24});

    const auto detail = store.get_document(inserted.document_id);
    CHECK(detail.has_value());
    CHECK_EQ(detail->text, std::string("A document about graphs."));
    CHECK_EQ(detail->chunks.size(), std::size_t{2});
    CHECK_EQ(detail->chunks[0].id, inserted.chunk_ids[0]);
    CHECK_EQ(detail->chunks[1].ordinal, 1);
    CHECK_EQ(detail->chunks[1].text, std::string("second chunk"));

    const auto missing = store.get_document(9999);
    CHECK(!missing.has_value());
}

HNSWHY_TEST(document_store_loads_embeddings_in_stable_order) {
    TempDb file("load");
    hnswhy::DocumentStore store(file.path());
    const hnswhy::InsertResult inserted = store.insert_document(
        "Embeddings", "seed", "text", {chunk("a", 1.0f), chunk("b", 2.0f)});

    const auto chunks = store.load_embedded_chunks();
    CHECK_EQ(chunks.size(), std::size_t{2});
    CHECK_EQ(chunks[0].chunk_id, inserted.chunk_ids[0]);
    CHECK_EQ(chunks[0].title, std::string("Embeddings"));
    CHECK_EQ(chunks[0].embedding.size(), std::size_t{4});
    CHECK_NEAR(chunks[0].embedding[0], 1.0f, 1e-7);
    CHECK_EQ(chunks[1].chunk_id, inserted.chunk_ids[1]);
    CHECK_NEAR(chunks[1].embedding[0], 2.0f, 1e-7);

    const auto details = store.get_chunks(inserted.chunk_ids);
    CHECK_EQ(details.size(), std::size_t{2});
    CHECK_EQ(details[1].text, std::string("b"));
    CHECK_EQ(details[0].ordinal, 0);
}

HNSWHY_TEST(document_store_delete_cascades_and_is_idempotent) {
    TempDb file("delete");
    hnswhy::DocumentStore store(file.path());
    const hnswhy::InsertResult first = store.insert_document(
        "First", "a", "text", {chunk("a", 1.0f)});
    const hnswhy::InsertResult second = store.insert_document(
        "Second", "b", "text", {chunk("b", 2.0f)});

    CHECK(store.delete_document(first.document_id));
    CHECK(!store.delete_document(first.document_id));
    CHECK_EQ(store.list_documents().size(), std::size_t{1});
    CHECK_EQ(store.chunk_ids_for_document(first.document_id).size(), std::size_t{0});
    CHECK_EQ(store.get_chunks(first.chunk_ids).size(), std::size_t{0});

    const hnswhy::StoreStats stats = store.stats();
    CHECK_EQ(stats.documents, std::size_t{1});
    CHECK_EQ(stats.chunks, std::size_t{1});
    CHECK_EQ(stats.embeddings, std::size_t{1});
    const auto remaining = store.list_documents();
    CHECK_EQ(remaining.size(), std::size_t{1});
    CHECK_EQ(remaining[0].id, second.document_id);
}

HNSWHY_TEST(document_store_refuses_a_second_writer) {
    TempDb file("single-writer");
    hnswhy::DocumentStore first(file.path());
    // A second server on the same file would keep its own in-memory index and
    // diverge from the first one, so opening must fail while the lock is held.
    bool refused = false;
    try {
        hnswhy::DocumentStore second(file.path());
    } catch (const std::runtime_error&) {
        refused = true;
    }
    CHECK(refused);
}

HNSWHY_TEST(document_store_reopens_without_losing_data) {
    TempDb file("reopen");
    hnswhy::Id document_id = 0;
    hnswhy::Id chunk_id = 0;
    {
        hnswhy::DocumentStore store(file.path());
        const hnswhy::InsertResult inserted = store.insert_document(
            "Persistent", "seed", "persistent text", {chunk("persistent chunk", 3.0f)});
        document_id = inserted.document_id;
        chunk_id = inserted.chunk_ids[0];
        store.set_meta("app_initialized", "1");
    }

    hnswhy::DocumentStore reopened(file.path());
    CHECK_EQ(reopened.stats().documents, std::size_t{1});
    const auto detail = reopened.get_document(document_id);
    CHECK(detail.has_value());
    CHECK_EQ(detail->chunks[0].id, chunk_id);
    CHECK_EQ(reopened.meta("app_initialized").value_or(""), std::string("1"));
}

HNSWHY_TEST(document_store_embedding_metadata_round_trip) {
    TempDb file("meta");
    hnswhy::DocumentStore store(file.path());
    CHECK(store.embedding_meta().empty());

    const hnswhy::EmbeddingMeta meta{"hash", "local-hash-v1", 256};
    store.set_embedding_meta(meta);
    const hnswhy::EmbeddingMeta loaded = store.embedding_meta();
    CHECK_EQ(loaded.provider, std::string("hash"));
    CHECK_EQ(loaded.model, std::string("local-hash-v1"));
    CHECK_EQ(loaded.dim, std::size_t{256});
}

HNSWHY_TEST(document_store_replaces_embeddings_atomically) {
    TempDb file("replace");
    hnswhy::DocumentStore store(file.path());
    const hnswhy::InsertResult inserted = store.insert_document(
        "Replace", "seed", "text", {chunk("a", 1.0f), chunk("b", 2.0f)});

    const std::vector<hnswhy::StoredEmbedding> replacements = {
        {inserted.chunk_ids[0], {0.0f, 1.0f, 0.0f}},
        {inserted.chunk_ids[1], {0.0f, 0.0f, 1.0f}},
    };
    store.replace_embeddings(replacements, {"openai", "text-embedding-3-small", 3});

    const auto chunks = store.load_embedded_chunks();
    CHECK_EQ(chunks[0].embedding.size(), std::size_t{3});
    CHECK_NEAR(chunks[0].embedding[1], 1.0f, 1e-7);
    CHECK_EQ(store.embedding_meta().dim, std::size_t{3});
}

HNSWHY_TEST(document_store_rolls_back_a_failed_embedding_replacement) {
    TempDb file("replace_fail");
    hnswhy::DocumentStore store(file.path());
    const hnswhy::InsertResult inserted = store.insert_document(
        "Replace", "seed", "text", {chunk("a", 1.0f), chunk("b", 2.0f)});

    const auto before_chunks = store.load_embedded_chunks();
    const hnswhy::EmbeddingMeta before_meta = store.embedding_meta();
    CHECK_EQ(before_chunks.size(), std::size_t{2});

    // The first replacement is valid and the second references a chunk that
    // does not exist. SQLite's foreign key rejects it; the transaction must
    // roll back the first row and the metadata update too.
    const std::vector<hnswhy::StoredEmbedding> replacements = {
        {inserted.chunk_ids[0], {0.0f, 1.0f, 0.0f}},
        {999999, {0.0f, 0.0f, 1.0f}},
    };
    bool rejected = false;
    try {
        store.replace_embeddings(replacements, {"openai", "test", 3});
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected);

    const auto after_chunks = store.load_embedded_chunks();
    const hnswhy::EmbeddingMeta after_meta = store.embedding_meta();
    CHECK_EQ(after_chunks.size(), before_chunks.size());
    for (std::size_t i = 0; i < after_chunks.size(); ++i) {
        CHECK_EQ(after_chunks[i].chunk_id, before_chunks[i].chunk_id);
        CHECK_EQ(after_chunks[i].embedding.size(), before_chunks[i].embedding.size());
        for (std::size_t j = 0; j < after_chunks[i].embedding.size(); ++j) {
            CHECK_NEAR(after_chunks[i].embedding[j], before_chunks[i].embedding[j], 0.0);
        }
    }
    CHECK_EQ(after_meta.provider, before_meta.provider);
    CHECK_EQ(after_meta.model, before_meta.model);
    CHECK_EQ(after_meta.dim, before_meta.dim);
}

HNSWHY_TEST(document_store_reports_incomplete_metadata_without_throwing) {
    TempDb file("badmeta");
    hnswhy::DocumentStore store(file.path());
    store.set_meta("embedding_provider", "hash");
    const hnswhy::EmbeddingMeta partial = store.embedding_meta();
    CHECK(!partial.empty());
    CHECK(!partial.complete());
    CHECK_EQ(partial.provider, std::string("hash"));
    CHECK_EQ(partial.dim, std::size_t{0});
    CHECK_THROWS_AS(store.set_embedding_meta({"", "model", 4}), std::invalid_argument);
    CHECK_THROWS_AS(store.set_embedding_meta({"hash", "", 4}), std::invalid_argument);
    CHECK_THROWS_AS(store.set_embedding_meta({"hash", "model", 0}), std::invalid_argument);
}

HNSWHY_TEST(document_store_replaces_a_missing_embedding_row) {
    TempDb file("upsert");
    hnswhy::DocumentStore store(file.path());
    const hnswhy::InsertResult inserted = store.insert_document(
        "Repair", "seed", "text", {chunk("a", 1.0f), chunk("b", 2.0f)});

    exec_sql(file.path(), "DELETE FROM embeddings WHERE chunk_id = " +
                              std::to_string(inserted.chunk_ids[1]));
    CHECK_THROWS_AS(store.load_embedded_chunks(), std::runtime_error);

    const std::vector<hnswhy::StoredEmbedding> replacements = {
        {inserted.chunk_ids[0], {1.0f, 0.0f, 0.0f}},
        {inserted.chunk_ids[1], {0.0f, 1.0f, 0.0f}},
    };
    store.replace_embeddings(replacements, {"hash", "local-hash-v1", 3});
    const auto chunks = store.load_embedded_chunks();
    CHECK_EQ(chunks.size(), std::size_t{2});
    CHECK_EQ(chunks[1].embedding.size(), std::size_t{3});
    CHECK_NEAR(chunks[1].embedding[1], 1.0f, 1e-7);
}

HNSWHY_TEST(document_store_rejects_a_corrupt_embedding_blob) {
    TempDb file("corrupt");
    hnswhy::DocumentStore store(file.path());
    const hnswhy::InsertResult inserted = store.insert_document(
        "Corrupt", "seed", "text", {chunk("a", 1.0f)});

    // The stored dimension disagrees with the blob length.
    exec_sql(file.path(), "UPDATE embeddings SET dim = 3 WHERE chunk_id = " +
                              std::to_string(inserted.chunk_ids[0]));
    CHECK_THROWS_AS(store.load_embedded_chunks(), std::runtime_error);

    // The dimension is plausible but the blob is truncated.
    exec_sql(file.path(),
             "UPDATE embeddings SET dim = 4, vector = x'0000' WHERE chunk_id = " +
                 std::to_string(inserted.chunk_ids[0]));
    CHECK_THROWS_AS(store.load_embedded_chunks(), std::runtime_error);
}

HNSWHY_TEST(document_store_samples_and_reports_stats) {
    TempDb file("sample");
    hnswhy::DocumentStore store(file.path());
    store.insert_document("One", "a", "text", {chunk("a", 1.0f), chunk("b", 2.0f)});
    store.insert_document("Two", "b", "text", {chunk("c", 3.0f)});

    CHECK_EQ(store.sample_chunks(10).size(), std::size_t{3});
    CHECK_EQ(store.sample_chunks(2).size(), std::size_t{2});
    CHECK_EQ(store.sample_chunks(0).size(), std::size_t{0});
    CHECK_EQ(store.stats().documents, std::size_t{2});
    CHECK_EQ(store.stats().chunks, std::size_t{3});
    CHECK_EQ(store.stats().embeddings, std::size_t{3});
}

HNSWHY_TEST(document_store_rejects_inconsistent_embeddings) {
    TempDb file("inconsistent");
    hnswhy::DocumentStore store(file.path());
    CHECK_THROWS_AS(
        store.insert_document("Bad", "a", "text",
                              {chunk("a", 1.0f), hnswhy::NewChunk{"b", {1.0f, 2.0f}}}),
        hnswhy::ProductError);
    CHECK_EQ(store.stats().documents, std::size_t{0});
}

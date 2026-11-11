#include "product/document_store.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <sqlite3.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "product/errors.h"

namespace hnswhy {
namespace {

[[noreturn]] void throw_sqlite(sqlite3* db, const std::string& action) {
    throw std::runtime_error("sqlite: " + action + ": " + sqlite3_errmsg(db));
}

void check_sqlite(sqlite3* db, int rc, const std::string& action) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw_sqlite(db, action);
    }
}

std::string now_utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm broken{};
    gmtime_r(&seconds, &broken);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &broken);
    return buffer;
}

class Statement {
public:
    Statement(sqlite3* db, std::string_view sql) : db_(db) {
        check_sqlite(db_, sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()),
                                             &stmt_, nullptr),
                     "prepare");
    }

    ~Statement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void reset() {
        check_sqlite(db_, sqlite3_reset(stmt_), "reset");
        // Reused statements must not inherit a value bound on the previous
        // iteration for a parameter the next caller does not set.
        check_sqlite(db_, sqlite3_clear_bindings(stmt_), "clear bindings");
    }

    void bind_text(int index, std::string_view value) {
        // A default-constructed string_view has a null data pointer, which
        // SQLite treats as SQL NULL rather than an empty string. Point empty
        // values at a real empty buffer so NOT NULL columns stay valid.
        const char* data = value.empty() ? "" : value.data();
        check_sqlite(db_,
                     sqlite3_bind_text(stmt_, index, data,
                                       static_cast<int>(value.size()), SQLITE_TRANSIENT),
                     "bind text");
    }

    void bind_int64(int index, std::int64_t value) {
        check_sqlite(db_, sqlite3_bind_int64(stmt_, index, value), "bind int64");
    }

    void bind_int(int index, int value) {
        check_sqlite(db_, sqlite3_bind_int(stmt_, index, value), "bind int");
    }

    void bind_blob(int index, const void* data, int size) {
        check_sqlite(db_, sqlite3_bind_blob(stmt_, index, data, size, SQLITE_TRANSIENT),
                     "bind blob");
    }

    bool step() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        throw_sqlite(db_, "step");
    }

    std::int64_t column_int64(int column) const {
        return sqlite3_column_int64(stmt_, column);
    }

    int column_int(int column) const { return sqlite3_column_int(stmt_, column); }

    std::string column_text(int column) const {
        const auto* text = sqlite3_column_text(stmt_, column);
        if (text == nullptr) {
            return {};
        }
        const int bytes = sqlite3_column_bytes(stmt_, column);
        return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(bytes));
    }

    const void* column_blob(int column) const { return sqlite3_column_blob(stmt_, column); }
    int column_bytes(int column) const { return sqlite3_column_bytes(stmt_, column); }
    bool column_is_null(int column) const {
        return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
    }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db) {
        char* error = nullptr;
        const int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            const std::string message = error == nullptr ? sqlite3_errmsg(db_) : error;
            sqlite3_free(error);
            throw std::runtime_error("sqlite: begin transaction: " + message);
        }
    }

    ~Transaction() {
        if (active_) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        char* error = nullptr;
        const int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            const std::string message = error == nullptr ? sqlite3_errmsg(db_) : error;
            sqlite3_free(error);
            throw std::runtime_error("sqlite: commit: " + message);
        }
        active_ = false;
    }

private:
    sqlite3* db_;
    bool active_ = true;
};

void validate_embedding_values(const std::vector<float>& values) {
    if (values.empty() || values.size() > 65536) {
        throw std::runtime_error("sqlite: embedding dimension is out of range");
    }
    for (const float value : values) {
        if (!std::isfinite(value)) {
            throw std::runtime_error("sqlite: embedding contains a non-finite value");
        }
    }
}

std::vector<float> read_embedding(const Statement& statement, int dim_column, int blob_column,
                                  Id chunk_id) {
    const int dim = statement.column_int(dim_column);
    const int bytes = statement.column_bytes(blob_column);
    const std::size_t expected_bytes =
        dim > 0 && dim <= 65536 ? static_cast<std::size_t>(dim) * sizeof(float) : 0;
    if (dim <= 0 || dim > 65536 || bytes < 0 ||
        static_cast<std::size_t>(bytes) != expected_bytes) {
        throw std::runtime_error("sqlite: embedding for chunk " + std::to_string(chunk_id) +
                                 " has an invalid dimension or size");
    }
    const void* blob = statement.column_blob(blob_column);
    if (blob == nullptr) {
        throw std::runtime_error("sqlite: embedding for chunk " + std::to_string(chunk_id) +
                                 " is null");
    }
    std::vector<float> values(static_cast<std::size_t>(dim));
    std::memcpy(values.data(), blob, expected_bytes);
    return values;
}

}  // namespace

DocumentStore::DocumentStore(const std::filesystem::path& path) : path_(path) {
    if (path != ":memory:") {
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                throw std::runtime_error("cannot create database directory: " +
                                         parent.string() + ": " + error.message());
            }
        }

        // One writer per database. O_CLOEXEC matters: pdftotext is spawned
        // while the server runs and must not inherit (and outlive) the lock.
        const std::filesystem::path lock_path(path.string() + ".lock");
        lock_.fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lock_.fd < 0) {
            throw std::runtime_error("cannot open database lock " + lock_path.string() + ": " +
                                     std::strerror(errno));
        }
        if (::flock(lock_.fd, LOCK_EX | LOCK_NB) != 0) {
            const int saved_errno = errno;
            ::close(lock_.fd);
            lock_.fd = -1;
            if (saved_errno == EWOULDBLOCK) {
                throw std::runtime_error(
                    "database is already open by another hnswhy process: " + path.string());
            }
            throw std::runtime_error("cannot lock database " + path.string() + ": " +
                                     std::strerror(saved_errno));
        }
    }

    const std::string filename = path == ":memory:" ? ":memory:" : path.string();
    const int rc = sqlite3_open_v2(filename.c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                       SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        const std::string message = db_ == nullptr ? "unknown error" : sqlite3_errmsg(db_);
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error("cannot open sqlite database " + filename + ": " + message);
    }

    try {
        sqlite3_busy_timeout(db_, 5000);
        char* error = nullptr;
        if (sqlite3_exec(db_, "PRAGMA foreign_keys=ON", nullptr, nullptr, &error) != SQLITE_OK) {
            const std::string message = error == nullptr ? sqlite3_errmsg(db_) : error;
            sqlite3_free(error);
            throw std::runtime_error("sqlite: enable foreign keys: " + message);
        }
        if (sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, &error) != SQLITE_OK) {
            const std::string message = error == nullptr ? sqlite3_errmsg(db_) : error;
            sqlite3_free(error);
            throw std::runtime_error("sqlite: enable WAL: " + message);
        }
        initialize_schema();
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }

    if (path != ":memory:") {
        // The database holds uploaded document text and embeddings. Restrict
        // it (and SQLite's sidecar files) to the owner instead of relying on
        // the process umask, which is commonly 022.
        const std::filesystem::perms owner_only =
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
        for (const std::filesystem::path& file :
             {path, std::filesystem::path(path.string() + "-wal"),
              std::filesystem::path(path.string() + "-shm"),
              std::filesystem::path(path.string() + ".lock")}) {
            std::error_code error;
            if (std::filesystem::exists(file, error)) {
                std::filesystem::permissions(file, owner_only,
                                             std::filesystem::perm_options::replace, error);
                if (error) {
                    sqlite3_close(db_);
                    db_ = nullptr;
                    throw std::runtime_error("cannot restrict database permissions: " +
                                             file.string() + ": " + error.message());
                }
            }
        }
    }
}

DocumentStore::~DocumentStore() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

DocumentStore::FileLock::~FileLock() {
    if (fd >= 0) {
        ::flock(fd, LOCK_UN);
        ::close(fd);
    }
}

void DocumentStore::initialize_schema() {
    Statement version(db_, "PRAGMA user_version");
    int user_version = 0;
    if (version.step()) {
        user_version = version.column_int(0);
    }
    if (user_version > 1) {
        throw std::runtime_error("database schema version " + std::to_string(user_version) +
                                 " is newer than this build supports");
    }
    if (user_version == 1) {
        return;
    }

    Transaction transaction(db_);
    const char* schema = R"sql(
        CREATE TABLE IF NOT EXISTS documents (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            source TEXT NOT NULL,
            text TEXT NOT NULL,
            created_at TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS chunks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
            ordinal INTEGER NOT NULL,
            text TEXT NOT NULL,
            created_at TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS embeddings (
            chunk_id INTEGER PRIMARY KEY REFERENCES chunks(id) ON DELETE CASCADE,
            dim INTEGER NOT NULL,
            vector BLOB NOT NULL
        );
        CREATE TABLE IF NOT EXISTS meta (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_chunks_document
            ON chunks(document_id, ordinal);
        PRAGMA user_version = 1;
    )sql";

    char* error = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error == nullptr ? sqlite3_errmsg(db_) : error;
        sqlite3_free(error);
        throw std::runtime_error("sqlite: create schema: " + message);
    }
    transaction.commit();
}

InsertResult DocumentStore::insert_document(const std::string& title,
                                            const std::string& source,
                                            const std::string& text,
                                            const std::vector<NewChunk>& chunks) {
    if (title.empty() || text.empty() || chunks.empty()) {
        throw validation_error("document title, text and chunks must not be empty");
    }

    std::size_t dim = 0;
    for (const NewChunk& chunk : chunks) {
        if (chunk.text.empty()) {
            throw validation_error("a document chunk is empty");
        }
        validate_embedding_values(chunk.embedding);
        if (dim == 0) {
            dim = chunk.embedding.size();
        } else if (chunk.embedding.size() != dim) {
            throw validation_error("document chunks have inconsistent embedding dimensions");
        }
    }

    Transaction transaction(db_);
    const std::string created_at = now_utc();

    constexpr std::string_view document_sql =
        "INSERT INTO documents (title, source, text, created_at) VALUES (?, ?, ?, ?)";
    Statement document(db_, document_sql);
    document.bind_text(1, title);
    document.bind_text(2, source);
    document.bind_text(3, text);
    document.bind_text(4, created_at);
    document.step();
    const Id document_id = sqlite3_last_insert_rowid(db_);

    constexpr std::string_view chunk_sql =
        "INSERT INTO chunks (document_id, ordinal, text, created_at) VALUES (?, ?, ?, ?)";
    constexpr std::string_view embedding_sql =
        "INSERT INTO embeddings (chunk_id, dim, vector) VALUES (?, ?, ?)";
    Statement chunk_statement(db_, chunk_sql);
    Statement embedding_statement(db_, embedding_sql);

    InsertResult result;
    result.document_id = document_id;
    result.chunk_ids.reserve(chunks.size());
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        chunk_statement.reset();
        embedding_statement.reset();
        chunk_statement.bind_int64(1, document_id);
        chunk_statement.bind_int(2, static_cast<int>(i));
        chunk_statement.bind_text(3, chunks[i].text);
        chunk_statement.bind_text(4, created_at);
        chunk_statement.step();
        const Id chunk_id = sqlite3_last_insert_rowid(db_);

        embedding_statement.bind_int64(1, chunk_id);
        embedding_statement.bind_int(2, static_cast<int>(chunks[i].embedding.size()));
        embedding_statement.bind_blob(
            3, chunks[i].embedding.data(),
            static_cast<int>(chunks[i].embedding.size() * sizeof(float)));
        embedding_statement.step();

        result.chunk_ids.push_back(chunk_id);
    }

    transaction.commit();
    return result;
}

std::vector<DocumentSummary> DocumentStore::list_documents() const {
    constexpr std::string_view sql = R"sql(
        SELECT d.id, d.title, d.source, d.created_at,
               COUNT(c.id) AS chunk_count, length(d.text) AS char_count
        FROM documents d
        LEFT JOIN chunks c ON c.document_id = d.id
        GROUP BY d.id
        ORDER BY d.id DESC
    )sql";
    Statement statement(db_, sql);
    std::vector<DocumentSummary> documents;
    while (statement.step()) {
        DocumentSummary document;
        document.id = statement.column_int64(0);
        document.title = statement.column_text(1);
        document.source = statement.column_text(2);
        document.created_at = statement.column_text(3);
        document.chunk_count = static_cast<std::size_t>(statement.column_int64(4));
        document.char_count = static_cast<std::size_t>(statement.column_int64(5));
        documents.push_back(std::move(document));
    }
    return documents;
}

std::optional<DocumentDetail> DocumentStore::get_document(Id document_id) const {
    constexpr std::string_view sql = R"sql(
        SELECT d.id, d.title, d.source, d.created_at, d.text,
               (SELECT COUNT(*) FROM chunks c WHERE c.document_id = d.id) AS chunk_count,
               length(d.text) AS char_count
        FROM documents d
        WHERE d.id = ?
    )sql";
    Statement statement(db_, sql);
    statement.bind_int64(1, document_id);
    if (!statement.step()) {
        return std::nullopt;
    }

    DocumentDetail detail;
    detail.summary.id = statement.column_int64(0);
    detail.summary.title = statement.column_text(1);
    detail.summary.source = statement.column_text(2);
    detail.summary.created_at = statement.column_text(3);
    detail.text = statement.column_text(4);
    detail.summary.chunk_count = static_cast<std::size_t>(statement.column_int64(5));
    detail.summary.char_count = static_cast<std::size_t>(statement.column_int64(6));

    constexpr std::string_view chunks_sql =
        "SELECT id, document_id, ordinal, text FROM chunks WHERE document_id = ? ORDER BY ordinal";
    Statement chunks(db_, chunks_sql);
    chunks.bind_int64(1, document_id);
    while (chunks.step()) {
        ChunkRecord chunk;
        chunk.id = chunks.column_int64(0);
        chunk.document_id = chunks.column_int64(1);
        chunk.ordinal = chunks.column_int(2);
        chunk.text = chunks.column_text(3);
        detail.chunks.push_back(std::move(chunk));
    }
    return detail;
}

bool DocumentStore::document_exists(const std::string& title,
                                    const std::string& source) const {
    constexpr std::string_view sql =
        "SELECT 1 FROM documents WHERE title = ? AND source = ? LIMIT 1";
    Statement statement(db_, sql);
    statement.bind_text(1, title);
    statement.bind_text(2, source);
    return statement.step();
}

std::vector<Id> DocumentStore::chunk_ids_for_document(Id document_id) const {
    Statement statement(db_, "SELECT id FROM chunks WHERE document_id = ? ORDER BY ordinal");
    statement.bind_int64(1, document_id);
    std::vector<Id> ids;
    while (statement.step()) {
        ids.push_back(statement.column_int64(0));
    }
    return ids;
}

bool DocumentStore::delete_document(Id document_id) {
    Transaction transaction(db_);
    Statement exists(db_, "SELECT 1 FROM documents WHERE id = ?");
    exists.bind_int64(1, document_id);
    if (!exists.step()) {
        return false;
    }

    Statement remove(db_, "DELETE FROM documents WHERE id = ?");
    remove.bind_int64(1, document_id);
    remove.step();
    if (sqlite3_changes(db_) == 0) {
        return false;
    }
    transaction.commit();
    return true;
}

std::vector<EmbeddedChunk> DocumentStore::load_embedded_chunks() const {
    constexpr std::string_view sql = R"sql(
        SELECT c.id, c.document_id, c.ordinal, d.title, c.text,
               e.dim, e.vector
        FROM chunks c
        JOIN documents d ON d.id = c.document_id
        LEFT JOIN embeddings e ON e.chunk_id = c.id
        ORDER BY c.id
    )sql";
    Statement statement(db_, sql);
    std::vector<EmbeddedChunk> chunks;
    while (statement.step()) {
        EmbeddedChunk chunk;
        chunk.chunk_id = statement.column_int64(0);
        chunk.document_id = statement.column_int64(1);
        chunk.ordinal = statement.column_int(2);
        chunk.title = statement.column_text(3);
        chunk.text = statement.column_text(4);
        if (statement.column_is_null(5) || statement.column_is_null(6)) {
            throw std::runtime_error("sqlite: chunk " + std::to_string(chunk.chunk_id) +
                                     " has no embedding; the database is inconsistent");
        }
        chunk.embedding = read_embedding(statement, 5, 6, chunk.chunk_id);
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

std::vector<ChunkText> DocumentStore::load_chunk_texts() const {
    Statement statement(db_, "SELECT id, text FROM chunks ORDER BY id");
    std::vector<ChunkText> chunks;
    while (statement.step()) {
        chunks.push_back(ChunkText{statement.column_int64(0), statement.column_text(1)});
    }
    return chunks;
}

std::vector<ChunkDetail> DocumentStore::get_chunks(std::span<const Id> chunk_ids) const {
    if (chunk_ids.empty()) {
        return {};
    }

    std::string sql =
        "SELECT c.id, c.document_id, d.title, c.ordinal, c.text "
        "FROM chunks c JOIN documents d ON d.id = c.document_id WHERE c.id IN (";
    for (std::size_t i = 0; i < chunk_ids.size(); ++i) {
        if (i != 0) {
            sql += ',';
        }
        sql += '?';
    }
    sql += ')';

    Statement statement(db_, sql);
    for (std::size_t i = 0; i < chunk_ids.size(); ++i) {
        statement.bind_int64(static_cast<int>(i + 1), chunk_ids[i]);
    }

    std::vector<ChunkDetail> found;
    while (statement.step()) {
        ChunkDetail chunk;
        chunk.chunk_id = statement.column_int64(0);
        chunk.document_id = statement.column_int64(1);
        chunk.title = statement.column_text(2);
        chunk.ordinal = statement.column_int(3);
        chunk.text = statement.column_text(4);
        found.push_back(std::move(chunk));
    }

    // The caller (search results) relies on its own ordering.
    std::vector<ChunkDetail> ordered;
    ordered.reserve(found.size());
    for (const Id id : chunk_ids) {
        const auto it = std::find_if(found.begin(), found.end(),
                                     [id](const ChunkDetail& chunk) {
                                         return chunk.chunk_id == id;
                                     });
        if (it != found.end()) {
            ordered.push_back(*it);
        }
    }
    return ordered;
}

std::vector<MapChunk> DocumentStore::sample_chunks(std::size_t limit) const {
    if (limit == 0) {
        return {};
    }
    // Deterministic reservoir sample: one scan, no ORDER BY RANDOM() temp
    // sort under the application mutex. The seed keeps the map stable between
    // requests while still spreading points across the whole library.
    // Only the preview prefix is needed. Reading full chunk text for every
    // chunk would copy the entire library under the application mutex just to
    // keep at most `limit` samples.
    constexpr std::string_view sql = R"sql(
        SELECT c.id, c.document_id, d.title, c.ordinal, substr(c.text, 1, 512)
        FROM chunks c JOIN documents d ON d.id = c.document_id
        ORDER BY c.id
    )sql";
    Statement statement(db_, sql);
    std::mt19937_64 rng(42);
    std::vector<MapChunk> chunks;
    chunks.reserve(limit);
    std::size_t seen = 0;
    while (statement.step()) {
        MapChunk chunk;
        chunk.chunk_id = statement.column_int64(0);
        chunk.document_id = statement.column_int64(1);
        chunk.title = statement.column_text(2);
        chunk.ordinal = statement.column_int(3);
        chunk.text = statement.column_text(4);
        ++seen;
        if (chunks.size() < limit) {
            chunks.push_back(std::move(chunk));
            continue;
        }
        std::uniform_int_distribution<std::size_t> pick(0, seen - 1);
        const std::size_t slot = pick(rng);
        if (slot < limit) {
            chunks[slot] = std::move(chunk);
        }
    }
    std::sort(chunks.begin(), chunks.end(),
              [](const MapChunk& a, const MapChunk& b) { return a.chunk_id < b.chunk_id; });
    return chunks;
}

EmbeddingMeta DocumentStore::embedding_meta() const {
    constexpr std::string_view sql =
        "SELECT key, value FROM meta WHERE key IN "
        "('embedding_provider', 'embedding_model', 'embedding_dim')";
    Statement statement(db_, sql);
    EmbeddingMeta meta;
    while (statement.step()) {
        const std::string key = statement.column_text(0);
        const std::string value = statement.column_text(1);
        if (key == "embedding_provider") {
            meta.provider = value;
        } else if (key == "embedding_model") {
            meta.model = value;
        } else if (key == "embedding_dim") {
            try {
                std::size_t consumed = 0;
                const unsigned long long dim = std::stoull(value, &consumed);
                if (consumed != value.size() || dim == 0 || dim > 65536) {
                    throw std::out_of_range("dimension");
                }
                meta.dim = static_cast<std::size_t>(dim);
            } catch (const std::exception&) {
                throw std::runtime_error("sqlite: embedding_dim metadata is invalid");
            }
        }
    }
    // Return a partial record rather than throwing. Startup with --reembed
    // can repair incomplete metadata; callers that need a usable embedding
    // space must check complete().
    return meta;
}

void DocumentStore::set_embedding_meta(const EmbeddingMeta& meta) {
    if (meta.provider.empty() || meta.model.empty() || meta.dim == 0) {
        throw std::invalid_argument("set_embedding_meta: incomplete metadata");
    }
    Transaction transaction(db_);
    set_meta_internal("embedding_provider", meta.provider);
    set_meta_internal("embedding_model", meta.model);
    set_meta_internal("embedding_dim", std::to_string(meta.dim));
    transaction.commit();
}

void DocumentStore::clear_embedding_meta() {
    Transaction transaction(db_);
    Statement statement(db_,
                        "DELETE FROM meta WHERE key IN "
                        "('embedding_provider', 'embedding_model', 'embedding_dim')");
    statement.step();
    transaction.commit();
}

void DocumentStore::replace_embeddings(std::span<const StoredEmbedding> embeddings,
                                       const EmbeddingMeta& meta) {
    if (embeddings.empty()) {
        throw std::invalid_argument("replace_embeddings: no embeddings");
    }
    if (meta.provider.empty() || meta.model.empty() || meta.dim == 0) {
        throw std::invalid_argument("replace_embeddings: incomplete metadata");
    }

    for (const StoredEmbedding& embedding : embeddings) {
        if (embedding.vector.size() != meta.dim) {
            throw std::invalid_argument(
                "replace_embeddings: an embedding does not match the metadata dimension");
        }
        validate_embedding_values(embedding.vector);
    }

    Transaction transaction(db_);
    Statement statement(
        db_, "INSERT INTO embeddings (chunk_id, dim, vector) VALUES (?, ?, ?) "
             "ON CONFLICT(chunk_id) DO UPDATE SET dim = excluded.dim, vector = excluded.vector");
    for (const StoredEmbedding& embedding : embeddings) {
        statement.reset();
        statement.bind_int64(1, embedding.chunk_id);
        statement.bind_int(2, static_cast<int>(embedding.vector.size()));
        statement.bind_blob(3, embedding.vector.data(),
                            static_cast<int>(embedding.vector.size() * sizeof(float)));
        statement.step();
        if (sqlite3_changes(db_) != 1) {
            throw std::runtime_error("sqlite: cannot replace embedding for missing chunk " +
                                     std::to_string(embedding.chunk_id));
        }
    }
    set_meta_internal("embedding_provider", meta.provider);
    set_meta_internal("embedding_model", meta.model);
    set_meta_internal("embedding_dim", std::to_string(meta.dim));
    transaction.commit();
}

std::optional<std::string> DocumentStore::meta(std::string_view key) const {
    Statement statement(db_, "SELECT value FROM meta WHERE key = ?");
    statement.bind_text(1, key);
    if (!statement.step()) {
        return std::nullopt;
    }
    return statement.column_text(0);
}

void DocumentStore::set_meta(std::string_view key, std::string_view value) {
    if (key.empty()) {
        throw std::invalid_argument("set_meta: empty key");
    }
    set_meta_internal(key, value);
}

void DocumentStore::set_meta_internal(std::string_view key, std::string_view value) {
    constexpr std::string_view sql =
        "INSERT INTO meta (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value";
    Statement statement(db_, sql);
    statement.bind_text(1, key);
    statement.bind_text(2, value);
    statement.step();
}

StoreStats DocumentStore::stats() const {
    constexpr std::string_view sql = R"sql(
        SELECT (SELECT COUNT(*) FROM documents),
               (SELECT COUNT(*) FROM chunks),
               (SELECT COUNT(*) FROM embeddings)
    )sql";
    Statement statement(db_, sql);
    if (!statement.step()) {
        throw std::runtime_error("sqlite: cannot read database statistics");
    }
    StoreStats values;
    values.documents = static_cast<std::size_t>(statement.column_int64(0));
    values.chunks = static_cast<std::size_t>(statement.column_int64(1));
    values.embeddings = static_cast<std::size_t>(statement.column_int64(2));
    return values;
}

}  // namespace hnswhy

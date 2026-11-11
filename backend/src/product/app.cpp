#include "product/app.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "product/chunker.h"
#include "product/errors.h"
#include "product/text.h"

namespace hnswhy {
namespace {

constexpr std::size_t kMaxTextBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaxTitleChars = 200;
constexpr std::size_t kMaxSourceChars = 300;
constexpr std::size_t kEmbedBatch = 32;
constexpr std::size_t kMapPreviewBytes = 320;

// Full graph validation walks every edge, which is O(N * M0^2) and too costly
// on every upload/delete at large N. Debug builds and tests keep the check;
// release relies on insert/remove invariants plus the startup rebuild.
void validate_after_mutation(const VectorIndex& index) {
#ifndef NDEBUG
    index.validate();
#else
    (void)index;
#endif
}

std::string preview(std::string_view text, std::size_t limit = kMapPreviewBytes) {
    return truncate_utf8_words(text, limit);
}

// NUL is valid UTF-8, but SQLite's length() and substr() stop at the first NUL.
// Accepting it would make char_count and previews disagree with the stored text,
// so reject it at the same boundary that rejects binary uploads.
bool contains_nul(std::string_view text) {
    return text.find('\0') != std::string_view::npos;
}

std::string title_from_source(const std::string& source) {
    if (source.empty() || source == "paste" || source == "sample") {
        return "Untitled document";
    }
    const std::filesystem::path path(source);
    std::string stem = path.stem().string();
    if (stem.empty()) {
        return "Untitled document";
    }
    std::replace(stem.begin(), stem.end(), '_', ' ');
    return stem;
}

}  // namespace

Application::Application(AppConfig config)
    : config_(std::move(config)),
      store_(config_.db_path),
      embedder_(create_embedding_provider(config_.embedding)),
      chat_(create_chat_provider(config_.chat)),
      index_(config_.hnsw),
      rag_(store_, index_, *embedder_, *chat_, config_.query_ef) {
    if (config_.chunk_overlap_words >= config_.chunk_target_words) {
        throw ProductError("CONFIG_ERROR", "chunk overlap must be smaller than chunk words",
                           500);
    }
    initialize();
}

void Application::log(const std::string& message) const {
    std::cerr << "[hnswhy] " << message << '\n';
}

std::vector<std::vector<float>> Application::embed_all(
    const std::vector<std::string>& texts) const {
    std::vector<std::vector<float>> result;
    result.reserve(texts.size());
    for (std::size_t begin = 0; begin < texts.size(); begin += kEmbedBatch) {
        const std::size_t end = std::min(begin + kEmbedBatch, texts.size());
        std::vector<std::string> batch(texts.begin() + static_cast<std::ptrdiff_t>(begin),
                                       texts.begin() + static_cast<std::ptrdiff_t>(end));
        std::vector<std::vector<float>> embeddings = embedder_->embed(batch);
        if (embeddings.size() != batch.size()) {
            throw ProductError("PROVIDER_ERROR",
                               "the embedding provider returned the wrong number of vectors",
                               502);
        }
        for (std::vector<float>& embedding : embeddings) {
            if (embedding.empty() || !finite_embedding(embedding)) {
                throw ProductError("PROVIDER_ERROR",
                                   "the embedding provider returned an invalid vector", 502);
            }
            result.push_back(normalize_embedding(std::move(embedding)));
        }
    }
    return result;
}

void Application::ensure_embedding_compatibility_locked(std::size_t actual_dim) {
    if (actual_dim == 0) {
        throw ProductError("PROVIDER_ERROR", "the embedding provider returned a zero dimension",
                           502);
    }
    if (embedder_->dim() != 0 && embedder_->dim() != actual_dim) {
        throw ProductError("PROVIDER_ERROR",
                           "the configured embedding dimension does not match the provider output",
                           502);
    }

    const StoreStats stats = store_.stats();
    EmbeddingMeta meta = store_.embedding_meta();

    if (stats.chunks == 0) {
        // There is nothing to preserve. Drop stale tombstones/meta so a user
        // can intentionally switch providers after clearing the library.
        if (index_.slot_count() != 0) {
            index_.rebuild({});
        }
        if (!meta.empty() &&
            (meta.provider != embedder_->name() || meta.model != embedder_->model() ||
             meta.dim != actual_dim)) {
            store_.clear_embedding_meta();
            meta = {};
        }
        if (meta.empty()) {
            store_.set_embedding_meta(
                EmbeddingMeta{embedder_->name(), embedder_->model(), actual_dim});
        }
        return;
    }

    if (!meta.complete()) {
        throw ProductError(
            "EMBEDDING_MISMATCH",
            "the database contains embeddings with missing or incomplete provider metadata; "
            "restart with --reembed or import the documents again",
            409);
    }
    if (meta.provider != embedder_->name() || meta.model != embedder_->model() ||
        meta.dim != actual_dim) {
        throw ProductError(
            "EMBEDDING_MISMATCH",
            "the active embedding space is " + meta.provider + "/" + meta.model + " (" +
                std::to_string(meta.dim) + "d), but the configured provider is " +
                embedder_->name() + "/" + embedder_->model() + " (" +
                std::to_string(actual_dim) + "d). Restart with --reembed to rebuild vectors.",
            409);
    }
    if (index_.dim() != 0 && index_.dim() != actual_dim) {
        throw ProductError("EMBEDDING_MISMATCH",
                           "the live index dimension does not match the embedding provider", 409);
    }
}

Application::PreparedDocument Application::prepare_document(std::string title,
                                                             std::string source,
                                                             std::string text) {
    PreparedDocument prepared;
    prepared.title = trim_copy(title);
    prepared.source = trim_copy(source);
    prepared.text = trim_copy(normalize_newlines(text));

    if (prepared.title.empty()) {
        prepared.title = title_from_source(prepared.source);
    }
    if (prepared.source.empty()) {
        prepared.source = "upload";
    }
    if (!is_valid_utf8(prepared.title)) {
        throw validation_error("title must be valid UTF-8");
    }
    if (!is_valid_utf8(prepared.source)) {
        throw validation_error("source name must be valid UTF-8");
    }
    if (!is_valid_utf8(prepared.text)) {
        throw ProductError("INVALID_UTF8", "the document text is not valid UTF-8", 422);
    }
    if (contains_nul(prepared.title) || contains_nul(prepared.source) ||
        contains_nul(prepared.text)) {
        throw ProductError("INVALID_TEXT",
                           "the document must not contain NUL bytes", 422);
    }
    if (utf8_length(prepared.title) > kMaxTitleChars) {
        throw validation_error("title must be at most 200 characters");
    }
    if (utf8_length(prepared.source) > kMaxSourceChars) {
        throw validation_error("source name must be at most 300 characters");
    }
    if (prepared.text.empty()) {
        throw ProductError("EMPTY_DOCUMENT", "the document has no text", 422);
    }
    if (prepared.text.size() > kMaxTextBytes) {
        throw ProductError("DOCUMENT_TOO_LARGE", "the document text exceeds the 4 MB limit",
                           413);
    }

    ChunkOptions options;
    options.target_words = config_.chunk_target_words;
    options.overlap_words = config_.chunk_overlap_words;
    const std::vector<TextChunk> chunks = chunk_text(prepared.text, options);
    if (chunks.empty()) {
        throw ProductError("EMPTY_DOCUMENT", "the document has no usable text", 422);
    }

    std::vector<std::string> chunk_texts;
    chunk_texts.reserve(chunks.size());
    for (const TextChunk& chunk : chunks) {
        chunk_texts.push_back(chunk.text);
    }

    // The embedding provider can do network I/O; this whole phase runs before
    // the caller takes the application mutex.
    std::vector<std::vector<float>> embeddings = embed_all(chunk_texts);
    if (embeddings.empty()) {
        throw ProductError("PROVIDER_ERROR", "the embedding provider returned no vectors", 502);
    }
    const std::size_t actual_dim = embeddings.front().size();
    for (const std::vector<float>& embedding : embeddings) {
        if (embedding.size() != actual_dim) {
            throw ProductError("PROVIDER_ERROR",
                               "the embedding provider returned inconsistent dimensions", 502);
        }
    }

    prepared.chunks.reserve(chunks.size());
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        prepared.chunks.push_back(NewChunk{chunks[i].text, std::move(embeddings[i])});
    }
    return prepared;
}

DocumentSummary Application::insert_document_locked(PreparedDocument document) {
    // Another upload may have established the embedding space while this
    // document was being embedded, so compatibility is checked again under the
    // lock before anything is written.
    ensure_embedding_compatibility_locked(document.chunks.front().embedding.size());

    const InsertResult inserted = store_.insert_document(
        document.title, document.source, document.text, document.chunks);

    try {
        if (inserted.chunk_ids.size() != document.chunks.size()) {
            throw std::runtime_error("document store returned a mismatched chunk count");
        }
        std::vector<IndexedVector> records;
        records.reserve(inserted.chunk_ids.size());
        for (std::size_t i = 0; i < inserted.chunk_ids.size(); ++i) {
            records.push_back(IndexedVector{inserted.chunk_ids[i], document.chunks[i].embedding});
        }
        index_.add(records);
        validate_after_mutation(index_);
    } catch (const std::exception& error) {
        log(std::string("incremental index update failed, rebuilding from SQLite: ") +
            error.what());
        rebuild_index_locked();
    }

    const auto detail = store_.get_document(inserted.document_id);
    if (!detail.has_value()) {
        throw std::runtime_error("document was inserted but could not be read back");
    }
    return detail->summary;
}

DocumentSummary Application::add_document(std::string title, std::string source,
                                           std::string text) {
    PreparedDocument prepared =
        prepare_document(std::move(title), std::move(source), std::move(text));
    std::lock_guard<std::mutex> lock(mutex_);
    return insert_document_locked(std::move(prepared));
}

std::vector<DocumentSummary> Application::list_documents() {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.list_documents();
}

std::optional<DocumentDetail> Application::get_document(Id document_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.get_document(document_id);
}

bool Application::delete_document(Id document_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::vector<Id> chunk_ids = store_.chunk_ids_for_document(document_id);
    if (!store_.delete_document(document_id)) {
        return false;
    }
    try {
        index_.remove(chunk_ids);
        validate_after_mutation(index_);
    } catch (const std::exception& error) {
        log(std::string("tombstone update failed, rebuilding from SQLite: ") + error.what());
        rebuild_index_locked();
        return true;
    }

    // Tombstones keep their slots and vectors alive. Compact once they are a
    // meaningful fraction of the library, or when nothing live remains, so a
    // delete/re-upload loop cannot grow memory without bound.
    const std::size_t live = index_.live();
    const std::size_t tombstones =
        index_.slot_count() >= live ? index_.slot_count() - live : 0;
    const std::size_t tombstone_limit = std::max<std::size_t>(1024, live / 5);
    if (live == 0 || tombstones > tombstone_limit) {
        log("compacting " + std::to_string(tombstones) +
            " tombstones by rebuilding the index from SQLite");
        rebuild_index_locked();
    }
    return true;
}

RagAnswer Application::ask(std::string_view question, std::size_t k) {
    // Validate before the empty check so a malformed question still gets a
    // 400 even when there is nothing to search.
    const std::string normalized = normalize_question(question);
    validate_k(k);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index_.live() == 0) {
            // Nothing is indexed, so a provider outage must not turn "no
            // documents" into a 503, and there is no reason to pay embedding
            // latency for a question that cannot match anything.
            return rag_.no_answer();
        }
    }

    // The embedding and chat providers may perform slow network calls. Only
    // the retrieval step touches the index and store, so keep the mutex around
    // that step alone: uploads and deletes stay responsive during generation.
    const RagQuery query = rag_.embed_question(normalized);
    RagAnswer answer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        answer = rag_.retrieve(query, k);
        if (answer.sources.empty()) {
            return answer;
        }
    }
    return rag_.generate(query, std::move(answer));
}

MapView Application::map(std::size_t limit) {
    if (limit == 0 || limit > 2000) {
        throw validation_error("limit must be between 1 and 2000");
    }
    // The dimension lives in the index, which uploads and rebuilds mutate.
    // Take the same lock before reading it so the limit check cannot race
    // with an incremental insert.
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t dim = index_.dim();
    if (dim != 0) {
        // A 4096-dimensional embedding makes the default 1,200 points a
        // multi-megabyte JSON response. Cap the sampled points by total float
        // count instead of rejecting the request; the map is a sample anyway.
        constexpr std::size_t kMaxMapFloats = 400'000;
        limit = std::min(limit, std::max<std::size_t>(1, kMaxMapFloats / dim));
    }
    MapView view;
    view.total_chunks = store_.stats().chunks;
    const std::vector<MapChunk> sampled = store_.sample_chunks(limit);
    view.points.reserve(sampled.size());
    for (const MapChunk& chunk : sampled) {
        const auto vector = index_.vector_for_chunk(chunk.chunk_id);
        if (!vector.has_value()) {
            continue;
        }
        MapPoint point;
        point.chunk_id = chunk.chunk_id;
        point.document_id = chunk.document_id;
        point.title = chunk.title;
        point.ordinal = chunk.ordinal;
        point.preview = preview(chunk.text);
        point.vector.assign(vector->begin(), vector->end());
        view.points.push_back(std::move(point));
    }
    std::sort(view.points.begin(), view.points.end(),
              [](const MapPoint& a, const MapPoint& b) {
                  return a.chunk_id < b.chunk_id;
              });
    return view;
}

StatusView Application::status() {
    std::lock_guard<std::mutex> lock(mutex_);
    StatusView view;
    // The API exposes a filename only. An absolute server path leaks the
    // deployment's directory layout to every browser that can reach /api.
    view.database = store_.path().filename().string();
    view.store = store_.stats();
    view.index_live = index_.live();
    view.index_slots = index_.slot_count();
    view.index_tombstones = index_.slot_count() - index_.live();
    view.index_rebuilds = index_.rebuild_count();
    view.dim = index_.dim();
    view.embedding_dim = index_.dim() != 0 ? index_.dim() : embedder_->dim();
    view.embedding_provider = embedder_->name();
    view.embedding_model = embedder_->model();
    view.chat_provider = chat_->name();
    view.chat_model = chat_->model();
    view.query_ef = config_.query_ef;
    view.sample_loaded = store_.meta("sample_loaded").has_value();
    std::error_code error;
    view.benchmark_available =
        std::filesystem::is_regular_file(config_.bench_report, error);
    return view;
}

HealthView Application::health() {
    HealthView view;
    // Health must stay responsive while an upload holds the application mutex.
    // SQLite is opened in serialized mode and the provider pointers are
    // immutable after construction, so this read is safe without the content
    // lock. The counts can briefly include rows from an upload that has not
    // committed yet; they are used for liveness, not for exact inventory.
    try {
        view.store = store_.stats();
        view.database = "ok";
        view.ok = true;
    } catch (const std::exception&) {
        view.database = "error";
        view.ok = false;
    }
    view.embedding.provider = embedder_->name();
    view.embedding.model = embedder_->model();
    view.chat.provider = chat_->name();
    view.chat.model = chat_->model();

    constexpr auto kProbeTtl = std::chrono::seconds(30);
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> probe_lock(probe_mutex_);
    if (providers_checked_at_.time_since_epoch().count() == 0 ||
        now - providers_checked_at_ >= kProbeTtl) {
        providers_embedding_available_ = embedder_->available();
        providers_chat_available_ = chat_->available();
        providers_checked_at_ = now;
    }
    view.embedding.available = providers_embedding_available_;
    view.chat.available = providers_chat_available_;
    return view;
}

BenchmarkFile Application::benchmark_json() const {
    BenchmarkFile result;
    std::error_code error;
    if (!std::filesystem::is_regular_file(config_.bench_report, error)) {
        return result;  // no report yet: not an error
    }
    const auto size = std::filesystem::file_size(config_.bench_report, error);
    if (error) {
        result.error = "the benchmark report exists but its size could not be read";
        return result;
    }
    if (size > 10U * 1024U * 1024U) {
        result.error = "the benchmark report is larger than the 10 MB response limit";
        return result;
    }
    std::ifstream input(config_.bench_report, std::ios::binary);
    if (!input) {
        result.error = "the benchmark report exists but could not be opened";
        return result;
    }
    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    if (contents.empty()) {
        result.error = "the benchmark report is empty";
        return result;
    }
    result.contents = std::move(contents);
    return result;
}

void Application::build_index_from_chunks_locked(const std::vector<EmbeddedChunk>& chunks) {
    if (chunks.empty()) {
        index_.rebuild({});
        return;
    }
    const std::size_t dim = chunks.front().embedding.size();
    std::vector<IndexedVector> records;
    records.reserve(chunks.size());
    for (const EmbeddedChunk& chunk : chunks) {
        if (chunk.embedding.size() != dim) {
            throw std::runtime_error("SQLite contains embeddings with inconsistent dimensions");
        }
        records.push_back(IndexedVector{chunk.chunk_id, chunk.embedding});
    }
    index_.rebuild(records);
    index_.validate();
}

void Application::rebuild_index_locked() {
    build_index_from_chunks_locked(store_.load_embedded_chunks());
}

void Application::reembed_all_locked() {
    const std::vector<ChunkText> chunks = store_.load_chunk_texts();
    if (chunks.empty()) {
        store_.clear_embedding_meta();
        index_.rebuild({});
        if (embedder_->dim() != 0) {
            store_.set_embedding_meta(
                EmbeddingMeta{embedder_->name(), embedder_->model(), embedder_->dim()});
        }
        return;
    }

    std::vector<std::string> texts;
    texts.reserve(chunks.size());
    for (const ChunkText& chunk : chunks) {
        texts.push_back(chunk.text);
    }
    std::vector<std::vector<float>> embeddings = embed_all(texts);
    if (embeddings.size() != chunks.size() || embeddings.empty()) {
        throw ProductError("PROVIDER_ERROR",
                           "the embedding provider returned the wrong number of vectors", 502);
    }
    const std::size_t dim = embeddings.front().size();
    for (const std::vector<float>& embedding : embeddings) {
        if (embedding.size() != dim) {
            throw ProductError("PROVIDER_ERROR",
                               "the embedding provider returned inconsistent dimensions", 502);
        }
    }
    if (embedder_->dim() != 0 && embedder_->dim() != dim) {
        throw ProductError("PROVIDER_ERROR",
                           "the configured embedding dimension does not match the provider output",
                           502);
    }

    std::vector<StoredEmbedding> stored;
    stored.reserve(chunks.size());
    std::vector<IndexedVector> records;
    records.reserve(chunks.size());
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        stored.push_back(StoredEmbedding{chunks[i].chunk_id, embeddings[i]});
        records.push_back(IndexedVector{chunks[i].chunk_id, embeddings[i]});
    }

    const EmbeddingMeta meta{embedder_->name(), embedder_->model(), dim};
    store_.replace_embeddings(stored, meta);
    index_.rebuild(records);
    index_.validate();
    log("re-embedded " + std::to_string(chunks.size()) + " chunks with " + meta.provider + "/" +
        meta.model);
}

bool Application::seed_sample_locked() {
    std::error_code error;
    if (config_.samples_dir.empty() ||
        !std::filesystem::is_directory(config_.samples_dir, error)) {
        log("sample knowledge base directory not found: " + config_.samples_dir.string());
        return true;
    }

    std::filesystem::directory_iterator iterator(config_.samples_dir, error);
    if (error) {
        log("cannot read sample knowledge base directory: " + error.message());
        return false;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : iterator) {
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error)) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if (extension == ".md" || extension == ".markdown" || extension == ".txt") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        log("sample knowledge base directory is empty");
        return true;
    }

    // Record that seeding started before the first write. A crash mid-way then
    // leaves a marker the next start can see, instead of a partial library that
    // looks like a finished initialization.
    store_.set_meta("sample_seeding", "1");

    std::size_t loaded = 0;
    std::size_t existing = 0;
    for (const auto& path : files) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            log("cannot read sample file: " + path.string());
            continue;
        }
        std::string text((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
        if (text.empty() || text.size() > kMaxTextBytes) {
            log("skipping empty or oversized sample file: " + path.string());
            continue;
        }
        std::string title = path.stem().string();
        std::replace(title.begin(), title.end(), '_', ' ');
        const std::size_t newline = text.find('\n');
        if (text.size() > 2 && text[0] == '#' && text[1] == ' ') {
            title = trim_copy(text.substr(2, newline == std::string::npos
                                                 ? std::string::npos
                                                 : newline - 2));
        }
        // A crash can interrupt seeding after some files are committed. Skip
        // the ones already present so the next start completes the set instead
        // of duplicating them or freezing a partial library.
        if (store_.document_exists(title, "sample")) {
            ++existing;
            continue;
        }
        try {
            insert_document_locked(prepare_document(title, "sample", std::move(text)));
            ++loaded;
        } catch (const std::exception& exception) {
            log("sample file failed: " + path.filename().string() + ": " + exception.what());
        }
    }
    if (loaded > 0 || existing > 0) {
        store_.set_meta("sample_loaded", "1");
    }
    log("seeded sample knowledge base: " + std::to_string(loaded) + " new, " +
        std::to_string(existing) + " already present");
    return loaded + existing > 0;
}

void Application::initialize() {
    if (config_.reembed) {
        // Re-embedding starts from chunk text, not from the old embedding
        // rows. That lets it repair missing embeddings and incomplete
        // provider metadata instead of failing before the flag is honored.
        // reembed_all_locked() already publishes the new graph; rebuilding it
        // again from SQLite would double startup time for no benefit.
        reembed_all_locked();
    } else {
        EmbeddingMeta meta = store_.embedding_meta();
        const std::vector<EmbeddedChunk> chunks = store_.load_embedded_chunks();

        const bool provider_mismatch =
            !meta.empty() && (!meta.complete() || meta.provider != embedder_->name() ||
                              meta.model != embedder_->model() ||
                              (embedder_->dim() != 0 && meta.dim != embedder_->dim()));
        const bool needs_reembed = !chunks.empty() && (meta.empty() || provider_mismatch);

        if (chunks.empty()) {
            if (!meta.empty()) {
                store_.clear_embedding_meta();
                meta = {};
            }
            if (embedder_->dim() != 0) {
                store_.set_embedding_meta(
                    EmbeddingMeta{embedder_->name(), embedder_->model(), embedder_->dim()});
            }
            index_.rebuild({});
        } else if (needs_reembed) {
            throw ProductError(
                "EMBEDDING_MISMATCH",
                "the database was embedded with a different provider or dimension. The stored "
                "space is " +
                    (meta.complete() ? meta.provider + "/" + meta.model
                                     : std::string("unknown")) +
                    "; the active provider is " + embedder_->name() + "/" +
                    embedder_->model() +
                    ". Restart with --reembed to rebuild all vectors.",
                500);
        } else {
            if (meta.dim == 0) {
                throw std::runtime_error(
                    "SQLite contains chunks without a valid embedding dimension");
            }
            for (const EmbeddedChunk& chunk : chunks) {
                if (chunk.embedding.size() != meta.dim) {
                    throw std::runtime_error(
                        "SQLite contains an embedding whose dimension does not match its metadata");
                }
            }
            build_index_from_chunks_locked(chunks);
        }
    }

    const bool initialized = store_.meta("app_initialized").has_value();
    if (!initialized) {
        const StoreStats current = store_.stats();
        // Seeding runs for a fresh library, or to finish a seed a crash
        // interrupted. The second case is why sample_seeding exists: without
        // it, a partially populated library would be marked initialized and
        // stay incomplete forever.
        const bool interrupted_seed =
            store_.meta("sample_seeding").has_value() &&
            !store_.meta("sample_loaded").has_value();
        const bool empty_library = current.documents == 0 && current.chunks == 0;
        bool seeding_finished = true;
        if (config_.load_sample && (empty_library || interrupted_seed)) {
            if (embedder_->name() == "hash") {
                seeding_finished = seed_sample_locked();
            } else {
                log("sample knowledge base skipped because it requires the offline hash "
                    "embedding provider");
            }
        }
        if (seeding_finished) {
            store_.set_meta("app_initialized", "1");
        } else {
            log("sample seeding did not load any documents; retrying on the next startup");
        }
    }

    log("ready: " + std::to_string(store_.stats().documents) + " documents, " +
        std::to_string(index_.live()) + " live vectors, " + std::to_string(index_.dim()) +
        " dimensions");
}

}  // namespace hnswhy

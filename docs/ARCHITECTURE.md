# VectorForge / HNSWhy — Architecture

> **Status: planning and merge.** No engine code exists in this repo yet. The
> frontend source exists in the separate VectorForge project and will be imported as
> `frontend/`. The decisions in `DECISIONS.md` are the lock; this file explains the
> shape that follows from them.
>
> **v1 in one line:** a RAG app a stranger can use, backed by a from-scratch C++ HNSW
> engine, measured against exact search on SIFT-100k, with tests and a reproducible
> benchmark.

---

## 0. Ten-second version

A user uploads documents. The backend splits them into chunks, asks Ollama for an
embedding per chunk, stores the text and vectors in SQLite, and inserts the vectors
into a from-scratch HNSW index. When the user asks a question, the backend embeds the
question, retrieves the nearest chunks with HNSW, and asks Ollama to answer using only
those chunks. The answer comes back with citations. A separate CLI measures the same
engine on SIFT-100k and writes `bench.json`; the UI's Benchmark tab reads that file.

The product is the demo. The engine is the depth. The benchmark is the evidence.

---

## 1. Why this shape

Three layers, three different jobs:

| Layer | What it is | Why it exists |
|---|---|---|
| **Product** | React UI, HTTP API, SQLite, Ollama | A stranger can use it without reading code |
| **Engine** | `VectorStore`, `BruteForce`, `HNSW`, metrics | The retrieval is actually implemented, not wrapped |
| **Evidence** | `hns_bench` CLI + `bench.json` | The engine's quality is measured, not asserted |

The boundaries are deliberate:

- The engine knows **slots, floats, distances, and graph links**. It does not know
  documents, SQL, HTTP, JSON, or Ollama.
- The product knows **documents, chunks, text, HTTP, SQLite, and prompts**. It does
  not implement HNSW or brute force.
- The benchmark uses the **same engine library** as the server. It is not a second
  implementation that can drift.
- The benchmark is a **CLI**, not an HTTP endpoint. Network and JSON latency do not
  contaminate the SIFT numbers.

This is a library plus a product, not a microservice architecture. A single C++
server process and a static frontend are enough for v1. Splitting the engine into its
own service would add deployment, serialization, and failure modes without improving
the claim.

---

## 2. System diagram

```text
┌────────────────────────────────────────────────────────────────────┐
│ Browser                                                            │
│  React UI (Vite)                                                   │
│  Documents │ Ask │ Map │ Benchmark                                 │
└───────────────────────────────┬────────────────────────────────────┘
                                │ HTTP JSON / multipart
┌───────────────────────────────▼────────────────────────────────────┐
│ C++ server (cpp-httplib)                                           │
│                                                                    │
│  api/routes        validation, JSON, static files, errors          │
│  product/rag       retrieve + prompt + citations                   │
│  product/store     SQLite: documents, chunks, embeddings           │
│  product/ollama    embed + generate                                │
│  engine/HNSW       in-memory graph over VectorStore                │
│  engine/BruteForce exact answer key (tests and benchmark)           │
└───────────────┬───────────────────────────────┬────────────────────┘
                │                               │
        ┌───────▼────────┐              ┌───────▼────────┐
        │ SQLite file    │              │ Ollama server  │
        │ source of truth│              │ embeddings+LLM │
        └────────────────┘              └────────────────┘

Separate process, same engine library:

  SIFT fvecs/ivecs ──► hns_bench ──► bench.json ──► GET /api/benchmark ──► UI
```

Two important separations:

1. **SQLite is the source of truth; HNSW is a derived index.** The graph is rebuilt
   from live embeddings on startup. There is no graph file to corrupt or version in
   v1.
2. **The benchmark is a controlled CLI run; the product is a live service.** The
   Benchmark tab reads a frozen report. It never triggers a 100k build inside a web
   request.

---

## 3. Components

### 3.1 Frontend

The existing VectorForge React/Vite app is the starting point, but the merge refactors
it. The current app has a three-tab shell (Search, Documents, Ask AI) with an
always-visible scatter plot; it talks to the old `/items` and `/doc/*` endpoints and
computes fake 16D embeddings for the old Search and visualization paths. v1
restructures it into the four tabs below, rewrites `api.js` for `/api/*`, and removes
the fake-embedding path. The visual language and reusable components are kept; the old
Search tab is not a v1 route.

| File | Responsibility |
|---|---|
| `frontend/src/App.jsx` | shell, tabs, global state |
| `frontend/src/api.js` | one place for HTTP calls and error handling |
| `components/DocumentsTab.jsx` | upload/paste, document list, chunk count, delete |
| `components/AskTab.jsx` | question, answer, citations, retrieved chunks, latency |
| `components/MapTab.jsx` | 2D PCA of real embeddings; query and hits highlighted |
| `components/BenchmarkTab.jsx` | table, curve, params, command, run metadata |
| `components/common/` | buttons, empty states, errors, loading, badges |
| `lib/pca.js` | small 2D PCA over the sampled embedding matrix |

Rules for the frontend:

- No fake embeddings. The old `textToEmbedding` with `Math.random()` jitter is
  removed.
- No benchmark numbers in the frontend. Numbers come from `bench.json` through the
  API.
- The map is labelled as a 2D projection, not a truth.
- The UI never calls Ollama directly. All model calls go through the backend.
- Every error state has a sentence a user can act on: "Ollama is not running at
  `localhost:11434`" is better than "Request failed."

### 3.2 API layer

`backend/src/api/routes.{h,cpp}`. One `main.cpp` only bootstraps.

Responsibilities:

- Parse and validate request bodies.
- Enforce size limits (document text, question length, `k`).
- Convert engine slots to chunk IDs and back.
- Return consistent JSON errors: `{ "error": { "code", "message" } }`.
- Serve the built frontend in production; in development, Vite proxies `/api`.
- Hold the API mutex around engine and document-index operations.

It does **not** contain HNSW logic, SQL strings scattered across handlers, or prompt
construction. Those belong to the product layer.

### 3.3 Product layer

| File | Responsibility |
|---|---|
| `product/document_store.{h,cpp}` | SQLite schema, transactions, CRUD, embeddings |
| `product/chunker.{h,cpp}` | split text into overlapping chunks |
| `product/ollama_client.{h,cpp}` | embed and generate over HTTP, timeouts, errors |
| `product/rag.{h,cpp}` | retrieve, build prompt, call generate, attach citations |

`DocumentStore` is the only code that writes SQL. It exposes typed methods, not a raw
database handle.

`OllamaClient` uses two endpoints:

- embeddings: `POST /api/embeddings` (or `/api/embed` depending on the Ollama
  version; keep one adapter and test it)
- generation: `POST /api/generate`

It must have timeouts. A hung Ollama call must not hang the whole server forever.

`RagService` is the product's retrieval story:

```text
question
  -> embed(question)
  -> normalize
  -> hnsw.search(query, k=5, ef=50)
  -> map slots to chunks
  -> build prompt with numbered sources
  -> ollama.generate(prompt)
  -> answer + citations + retrieved chunks + timings
```

If the index has no live chunks, return a clear "I could not find this in the uploaded
documents" response without calling the LLM. If the index has chunks, the constrained
prompt decides whether the sources answer; a refusal is returned with its sources, not
replaced by general knowledge. If Ollama is down, return `503` with a fixable message.
Never invent an answer from general knowledge and present it as document-grounded.

### 3.4 Engine library (`vf_engine`)

The engine is a static library linked by the server, the benchmark, and the tests.

| File | Responsibility |
|---|---|
| `engine/vector_store.{h,cpp}` | flat `std::vector<float>`, `reserve`, `add`, `get`, `size`, `dim` |
| `engine/distance.h` | squared L2, normalization, small helpers |
| `engine/brute_force.{h,cpp}` | exact top-k scan for tests and benchmark ground truth |
| `engine/hnsw.{h,cpp}` | graph build, insert, search, prune, mark-deleted, validate |
| `engine/metrics.{h,cpp}` | distance counts, timings, recall helpers |

The public engine surface is deliberately small:

```cpp
using Slot = uint32_t;

enum class PruneStrategy { Heuristic, NearestOnly };

struct HNSWParams {
    size_t M = 16;
    size_t M0 = 32;
    size_t efConstruction = 200;
    size_t maxLevel = 32;
    uint64_t seed = 42;
    PruneStrategy prune = PruneStrategy::Heuristic;
};

struct Candidate {
    float dist;
    Slot id;
};

struct SearchStats {
    uint64_t distanceComputations = 0;
    uint32_t hops = 0;
    uint32_t layerVisits = 0;
};

struct SearchResult {
    std::vector<Slot> ids;
    std::vector<float> distances;
    SearchStats stats;
};

class VectorStore {
public:
    void reserve(size_t n, size_t dim);
    Slot add(std::span<const float> v);
    std::span<const float> get(Slot id) const;
    size_t size() const;
    size_t dim() const;

private:
    std::vector<float> data_;
    size_t dim_ = 0;
    size_t size_ = 0;
};

class BruteForce {
public:
    explicit BruteForce(const VectorStore& store);
    SearchResult search(std::span<const float> query, size_t k) const;

private:
    const VectorStore& store_;
};

class HNSW {
public:
    HNSW(const VectorStore& store, HNSWParams params);

    void build();
    void insert(Slot id);
    void remove(Slot id);              // mark-deleted
    SearchResult search(std::span<const float> query, size_t k, size_t ef) const;
    void validate() const;

    size_t size() const;               // live vectors
    size_t slot_count() const;         // includes tombstones
    bool isDeleted(Slot id) const;

private:
    Slot greedySearchLayer(std::span<const float> query, Slot entry, int layer) const;
    std::vector<Candidate> searchLayer(std::span<const float> query,
                                       std::span<const Slot> entries,
                                       size_t ef, int layer,
                                       bool liveOnly) const;
    std::vector<Slot> selectNeighbours(Slot ref,
                                       std::span<const Candidate> candidates,
                                       size_t cap) const;
    void connect(Slot u, std::span<const Slot> neighbours, int layer);
    void prune(Slot u, int layer);
    int sampleLevel();

    const VectorStore& store_;
    HNSWParams params_;
    // layer 0: flat links + degrees
    // upper layers: sparse adjacency
    // entryPoint_, maxLevel_, deleted_, rng_
};
```

The graph details, invariants, and prune example are in `HNSW-SPEC.md`.

### 3.5 Benchmark CLI

`backend/bench/` builds `hns_bench`:

| File | Responsibility |
|---|---|
| `bench/main.cpp` | argument parsing, orchestration |
| `bench/fvecs.{h,cpp}` | read `.fvecs` / `.ivecs`, validate dimensions |
| `bench/report.{h,cpp}` | write `bench.json` with metrics and run metadata |

It links `vf_engine` and never links SQLite, Ollama, or the HTTP server. A benchmark
run should work on a machine with no database and no model server.

### 3.6 Tests

`backend/tests/` builds `hns_tests`, linking `vf_engine` (and, for integration tests,
the product layer). The test list and gates are in §11.

---

## 4. Data model and persistence

### 4.1 SQLite schema

```sql
CREATE TABLE documents (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    title      TEXT NOT NULL,
    text       TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE chunks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
    text        TEXT NOT NULL,
    created_at  TEXT NOT NULL
);

CREATE TABLE embeddings (
    chunk_id INTEGER PRIMARY KEY REFERENCES chunks(id) ON DELETE CASCADE,
    dim      INTEGER NOT NULL,
    vector   BLOB NOT NULL
);

CREATE INDEX idx_chunks_document ON chunks(document_id);
```

Open the connection with `PRAGMA foreign_keys = ON`; SQLite does not enforce the
cascade rules by default. Without it, deleting a document would leave orphan chunks and
embeddings.

`chunks.id` is the external chunk ID. Engine slots are runtime-only: on startup, chunks
are loaded in ID order and assigned slots `0..n-1`; the in-memory `slot_to_chunk_`
vector is the mapping. Slots may change across restarts, which is fine because the
graph is rebuilt at the same time. SQLite never stores an engine slot.

### 4.2 Startup

```text
open SQLite
  -> load chunks ordered by id
  -> load embeddings joined on chunk_id
  -> assign slot 0..n-1 in that order
  -> store_.reserve(n, dim); add each vector
  -> hnsw_.build()
  -> slot_to_chunk_ ready
  -> start HTTP server
```

The startup rebuild is part of the product's cost. It is measured for the product
dataset (for example, 10k chunks), not folded into the SIFT benchmark. Startup refuses
to serve if a chunk is missing its embedding or if dimensions are mixed; a broken
database is not silently indexed.

### 4.3 Insert

```text
validate title/text
  -> split into chunks
  -> embed each chunk (Ollama)
  -> normalize each vector
  -> begin SQLite transaction
       insert document
       insert chunks
       insert embeddings keyed by chunk_id
     commit
  -> for each new chunk: slot = store_.add(vector);
     hnsw_.insert(slot); slot_to_chunk_[slot] = chunk_id
  -> update live counts
```

SQLite commits first. If the process dies before the graph inserts, the next startup
rebuild repairs the graph. If an in-memory insert fails after commit, return an error
and rebuild; the database remains the source of truth.

### 4.4 Delete

```text
look up chunk IDs and their runtime slots
  -> DELETE the document row (cascade chunks and embeddings)
  -> hnsw_.remove(slot) for each
  -> update live counts
```

The node remains as a routing node in memory. On restart, the document is gone from
SQLite, so the rebuilt graph contains only live nodes. There is no physical graph
compaction in v1. To find the slots for a document's chunks, scan `slot_to_chunk_` (or
keep a small reverse map); this is O(live chunks) per delete and acceptable at v1 size.

### 4.5 Why not persist the graph?

A serialized HNSW graph is a second source of truth. It needs a format version, a
compatibility check, a corruption test, and a rule for what happens when SQLite and
the graph disagree. Rebuilding from embeddings is simpler and cannot drift. The cost
is startup time, which is acceptable at the v1 target size. A graph snapshot is a
stretch item with its own decision and corruption test.

---

## 5. Engine design in one page

- **Store.** One flat `std::vector<float>`. Slot `i` starts at `i * dim`. No
  per-vector allocation. `VectorStore` is the only owner of float data.
- **Brute force.** Scan every slot in the store, keep a max-heap of size `k`. This is
  the answer key for recall, tests, and benchmark ground truth. The product always
  searches HNSW; brute force is not a second product path.
- **HNSW.** Layered proximity graph. Layer 0 contains every node; upper layers are
  sparse shortcuts. Greedy descent above layer 0, beam search at layer 0.
- **Layer 0 storage.** `N * M0` `uint32_t` links plus a `uint16_t` degree per node.
  Pruning swap-erases within the node's fixed slots. No CSR.
- **Growth.** `VectorStore` and the HNSW adjacency arrays grow geometrically on insert;
  old slot IDs and links stay valid. Do not rebuild the index on every upload.
- **Upper layers.** Sparse; a small vector per node or per-layer slabs. The layer-0
  flat rule does not apply to these thin layers.
- **Directed graph.** Edges are added in both directions initially; pruning is
  one-sided. There is no symmetry invariant.
- **Mark-deleted.** Deleted nodes remain as routers, may be selected by insertion, and
  are filtered from query results.
- **Single-threaded.** One search at a time. The API serializes access.
- **No persistence in the engine.** The product rebuilds from SQLite; the benchmark
  builds once per run.
- **Parameters.** Default `M=16`, `M0=32`, `efConstruction=200`, `efSearch=50` for
  product queries; the benchmark sweeps `efSearch`.
- **Determinism.** Fixed seed, tie-break by slot ID, no unordered iteration in a
  result-affecting path.

The full algorithm contract is `HNSW-SPEC.md`.

---

## 6. Product flows

### 6.1 Upload and index

1. User uploads `.txt`/`.md` or pastes text.
2. Backend validates size and extracts plain text.
3. `Chunker` splits into ~350-word chunks with ~50-word overlap.
4. `OllamaClient::embed` returns one vector per chunk.
5. Vectors are normalized.
6. `DocumentStore` commits the document, chunks, and embeddings.
7. `HNSW::insert` adds each slot.
8. UI shows the document, chunk count, and indexing status.

PDF extraction is deliberately deferred. A PDF library is a new dependency and a new
failure surface; plain text and Markdown prove the flow.

### 6.2 Ask

1. User asks a question.
2. Backend embeds and normalizes it.
3. `HNSW::search(question, k=5, ef=50)` returns slots.
4. Backend maps slots to live chunks.
5. If the index has no live chunks, return a grounded "not found" response without
   calling the LLM.
6. Build a prompt with numbered sources and an instruction to answer only from them.
7. `OllamaClient::generate` returns the answer.
8. If the model refuses because the sources do not answer the question, return that
   refusal with the sources; do not substitute general knowledge.
9. Backend returns `{ answer, sources, retrieved, timings }`.
10. UI renders the answer with clickable citations.

The answer is not trusted just because it is fluent. The citations are the product's
honesty mechanism: a user can open the chunk that supposedly supports the sentence.

### 6.3 Map

1. Backend samples up to 2,000 live chunks (default 1,000) and returns their vectors
   plus document metadata.
2. Frontend computes a 2D PCA (`lib/pca.js`) and draws points.
3. The query and retrieved chunks are highlighted.
4. The UI says: "2D projection of real embeddings; distances are approximate."

The map is a visual aid, not a benchmark and not a proof of cluster quality. If PCA
looks messy, that is allowed; the embeddings are real.

### 6.4 Benchmark tab

1. Backend reads `docs/results/bench.json` (or the `--bench-report` path).
2. UI shows the SIFT-100k table, the recall–QPS curve, the chosen parameters, the
   command, and the run metadata.
3. If no report exists, the tab says how to produce one; it does not invent a chart.
4. The tab never runs a 100k benchmark in the request.

---

## 7. API contract

All endpoints are under `/api`. JSON in, JSON out, except document upload which is
`multipart/form-data`.

| Method | Path | Body / query | Returns |
|---|---|---|---|
| `GET` | `/api/health` | — | `{ status, ollama, db, vectors }` |
| `GET` | `/api/status` | — | counts, build info, benchmark availability |
| `POST` | `/api/documents` | multipart `file` or JSON `{title,text}` | `{ document_id, chunks }` |
| `GET` | `/api/documents` | — | `[{ id, title, chunks, created_at }]` |
| `DELETE` | `/api/documents/{id}` | — | `{ deleted: true }` |
| `POST` | `/api/ask` | `{ question, k? }` | `{ answer, sources, retrieved, timings }` |
| `GET` | `/api/map` | `?limit=1000` | `{ points: [{ chunk_id, document_id, title, vector }] }` |
| `GET` | `/api/benchmark` | — | `bench.json` or `{ available: false }` |

Error shape:

```json
{
  "error": {
    "code": "OLLAMA_UNAVAILABLE",
    "message": "Ollama is not reachable at localhost:11434. Start it with `ollama serve`."
  }
}
```

Validation rules:

- `question`: non-empty, max 2,000 characters.
- `k`: 1–20, default 5.
- document text: non-empty after trimming, max 2 MB per upload in v1.
- title: non-empty, max 200 characters.
- map `limit`: 1–2,000.
- `DELETE /api/documents/{id}`: return `404` if the document does not exist; otherwise
  delete and return `{ "deleted": true }`.

There is no auth in v1. The README must say the demo is for a local or trusted
environment, not a public multi-tenant service. If it is deployed publicly, put it
behind a simple access gate or accept that anyone can upload and delete documents.

---

## 8. Benchmark and measurement

The benchmark is a separate CLI:

```bash
./build/hns_bench \
  --base data/sift/sift_base.fvecs \
  --queries data/sift/sift_query.fvecs \
  --groundtruth data/sift/sift100k_groundtruth.ivecs \
  --groundtruth-dist data/sift/sift100k_groundtruth_dist.fvecs \
  --n 100000 --k 10 \
  --M 16 --efConstruction 200 --ef 50 \
  --ef-grid 10,20,50,100,200 \
  --trials 3 \
  --out docs/results/bench.json
```

The release report is committed under `docs/results/`; the dataset stays in
gitignored `data/`. A local scratch run may write `data/bench.json`.

Protocol:

1. Load and validate the base, queries, and ground truth.
2. Build the index once; record build time.
3. Warm up on 100 queries.
4. Run every `--ef-grid` point from the same build; for each point, run three timed
   passes over all 10,000 queries. `--ef` is the headline point.
5. Compute recall against the exact prefix ground truth.
6. Measure the exact-search baseline on the same queries, kernel, flags, and thread
   count; reuse the cached baseline when the dataset/query/k triple is unchanged.
7. Write `bench.json` with metrics, the curve, and metadata.

Metrics:

- `R@1`, `R@10`
- QPS
- p50 / p95 / p99 latency
- build time
- peak RSS
- bytes per vector
- average distance computations per query
- exact-search QPS and speedup
- parameter set and run metadata

The SLO rule:

> Pick the lowest-latency configuration whose `R@10` on `sift10k` is at least 0.90.
> Report the measured `R@10` and QPS on `sift100k` for that configuration. Do not
> claim a band; report the actual number. If `sift100k` falls below the 0.90 gate,
> report the miss and fix the configuration; do not move the gate.

The benchmark tab reads the report. It never computes a second headline.

The optional ablation compares `--prune nearest` with `--prune heuristic` at the
headline configuration. It is a later experiment, not a v1 UI panel; the two-cluster
test is the v1 evidence for the heuristic.

---

## 9. Concurrency and failure behavior

The engine is single-threaded. The API server may accept concurrent connections, but
it serializes all operations that touch the document store or the engine with one
mutex. This is a deliberate v1 tradeoff: correctness and a clean single-thread
benchmark over a half-finished concurrent index.

Failure behavior:

| Failure | Behavior |
|---|---|
| Ollama not running | `/api/ask` and upload return `503` with a fixable message |
| Ollama timeout | request fails after the configured timeout; server stays up |
| SQLite write fails | transaction rolls back; no partial document |
| Graph insert fails after DB commit | return error; rebuild on next start or trigger rebuild |
| Benchmark file missing | Benchmark tab shows instructions, not a fake chart |
| Embedding dimension mismatch | reject the document with a clear error |
| Empty question | `400` with a validation message |
| Empty index | grounded "not found" response; no LLM call |
| Model refuses from sources | return the refusal with sources; no fallback answer |

The server must not crash on a bad request, a missing file, or a model error. Those
are normal product states.

---

## 10. File layout and build targets

```text
vectorforge/                       # repo root (currently HNSWhy/)
├── CMakeLists.txt                 # adds backend/
├── CMakePresets.json              # release/test presets used by the README
├── frontend/
│   ├── package.json
│   ├── package-lock.json
│   ├── index.html
│   ├── vite.config.js
│   └── src/
│       ├── App.jsx
│       ├── api.js
│       ├── components/
│       │   ├── DocumentsTab.jsx
│       │   ├── AskTab.jsx
│       │   ├── MapTab.jsx
│       │   ├── BenchmarkTab.jsx
│       │   └── common/
│       └── lib/pca.js
│
├── backend/
│   ├── CMakeLists.txt
│   ├── third_party/
│   │   ├── httplib.h
│   │   └── json.hpp
│   ├── src/
│   │   ├── main.cpp                 # bootstrap only
│   │   ├── api/
│   │   │   └── routes.{h,cpp}
│   │   ├── product/
│   │   │   ├── document_store.{h,cpp}
│   │   │   ├── chunker.{h,cpp}
│   │   │   ├── ollama_client.{h,cpp}
│   │   │   └── rag.{h,cpp}
│   │   └── engine/
│   │       ├── vector_store.{h,cpp}
│   │       ├── distance.h
│   │       ├── brute_force.{h,cpp}
│   │       ├── hnsw.{h,cpp}
│   │       └── metrics.{h,cpp}
│   ├── bench/
│   │   ├── main.cpp
│   │   ├── fvecs.{h,cpp}
│   │   └── report.{h,cpp}
│   └── tests/
│       ├── test_distance.cpp
│       ├── test_store.cpp
│       ├── test_brute.cpp
│       ├── test_hnsw.cpp
│       ├── test_prune.cpp
│       ├── test_delete.cpp
│       ├── test_persistence.cpp
│       ├── test_api.cpp
│       └── test_bench.cpp
│
├── data/                            # gitignored
│   ├── sift/
│   └── vectorforge.db
├── scripts/
│   ├── fetch_sift.sh
│   └── run_bench.sh
├── docs/
│   └── results/
│       ├── README.md
│       └── bench.json             # frozen release artifact (no dataset)
├── README.md
└── .gitignore
```

That is roughly 25 non-test C++ source/header files (34 with tests) and a dozen
frontend files. There is no 1,000-line `main.cpp`: `main.cpp` opens the database,
loads the index, registers routes, and starts the server. The algorithm lives in
`engine/`; the SQL lives in `product/document_store`; the prompts live in
`product/rag`.

The root `CMakeLists.txt` only calls `add_subdirectory(backend)`; the targets live in
`backend/CMakeLists.txt`. The presets keep the build in root `build/`, matching the
README commands.

### CMake targets

```cmake
add_library(vf_engine STATIC
    src/engine/vector_store.cpp
    src/engine/brute_force.cpp
    src/engine/hnsw.cpp
    src/engine/metrics.cpp
)
target_include_directories(vf_engine PUBLIC src/engine)

add_executable(vectorforge_server
    src/main.cpp
    src/api/routes.cpp
    src/product/document_store.cpp
    src/product/chunker.cpp
    src/product/ollama_client.cpp
    src/product/rag.cpp
)
target_include_directories(vectorforge_server PRIVATE src third_party)
target_link_libraries(vectorforge_server PRIVATE vf_engine sqlite3)

add_executable(hns_bench
    bench/main.cpp
    bench/fvecs.cpp
    bench/report.cpp
)
target_include_directories(hns_bench PRIVATE bench)
target_link_libraries(hns_bench PRIVATE vf_engine)

add_executable(hns_tests
    tests/test_distance.cpp
    tests/test_store.cpp
    tests/test_brute.cpp
    tests/test_hnsw.cpp
    tests/test_prune.cpp
    tests/test_delete.cpp
    tests/test_persistence.cpp
    tests/test_api.cpp
    tests/test_bench.cpp
    bench/fvecs.cpp
    bench/report.cpp
    src/api/routes.cpp
    src/product/document_store.cpp
    src/product/chunker.cpp
    src/product/ollama_client.cpp
    src/product/rag.cpp
)
target_include_directories(hns_tests PRIVATE src bench third_party)
target_link_libraries(hns_tests PRIVATE vf_engine sqlite3)
```

`hns_tests` compiles the product and bench-support sources directly so the
persistence, API, and report-schema tests exercise the real code. Only the engine is a
shared library; there is no second engine implementation.

Dependencies: C++20, CMake, `cpp-httplib` (header-only), `nlohmann/json`
(header-only), SQLite3, Ollama running locally. The frontend uses the existing
React/Vite stack.

---

## 11. Testing

### 11.1 Engine tests

| Test | What it proves |
|---|---|
| `test_distance` | squared L2 is correct; normalization works; zero vector handled |
| `test_store` | add/get/reserve; dimension checks; no out-of-bounds |
| `test_brute` | exact top-k; ties deterministic by slot |
| `test_hnsw` | insert/search on a small graph; recall on a fixed fixture; invariants |
| `test_prune` | two-cluster test: nearest-only disconnects, heuristic keeps a bridge |
| `test_delete` | deleted IDs never returned; live neighbours still found; entry/top-layer safe |

### 11.2 Product tests

| Test | What it proves |
|---|---|
| `test_persistence` | insert -> restart -> rebuild -> same search results; delete -> restart -> gone |
| `test_api` | upload/ask/delete with a fake Ollama; error paths; no partial document |
| `test_bench` | fvecs/ivecs parsing; report schema; dirty-tree refusal |

### 11.3 CI

- Build `vf_engine`, `vectorforge_server`, `hns_bench`, `hns_tests`.
- Run all tests.
- Run `sift10k` with 1,000 queries and require `R@10 ≥ 0.90`.
- Run the two-cluster prune test.
- Run the persistence/restart test.
- No 100k benchmark and no QPS gate in CI; shared runners are not a benchmark
  machine.

Sanitizers: a separate `ASan+UBSan` build runs the engine and product tests. It is not
the release build.

---

## 12. Memory and performance expectations

Approximate, excluding allocator overhead, SQLite cache, and Ollama (separate
process):

| Dataset | Vectors | Layer-0 links | Total engine |
|---|---:|---:|---:|
| SIFT-100k, 128D | 51.2 MB | 12.8 MB | ~65 MB |
| Product, 10k chunks, 768D | 30.7 MB | 1.3 MB | ~33 MB |
| Product, 100k chunks, 768D | 307 MB | 12.8 MB | ~322 MB |
| SIFT-1M, 128D (optional) | 512 MB | 128 MB | ~651 MB |

The product's SQLite file is roughly the size of its vectors plus text. At 10k
chunks, the process is comfortable on a laptop. At 100k chunks, it is still possible
but the startup rebuild and the SQLite file become noticeable; that is the upper end
of the v1 target, not a promise of production scale.

The benchmark's RSS is measured for the `hns_bench` process. The API's RSS is
measured separately. Do not mix them.

---

## 13. v1, stretch, and non-goals

### Ships in v1

| Area | Ships |
|---|---|
| Product | upload/paste, list, delete, ask, citations, 2D map, benchmark tab |
| Persistence | SQLite source of truth; rebuild on startup |
| Engine | flat store, squared L2, brute force, HNSW insert/search/prune, mark-deleted |
| Evidence | SIFT-100k, 10k queries, 3 trials per curve point, `bench.json`, README table |
| Safety | unit/invariant tests, two-cluster test, recall gate on `sift10k`, persistence test |
| Ops | documented local run; documented configuration; no auth |

### Stretch (after v1)

- One SIFT-1M configuration.
- Graph snapshot with a corruption test.
- Physical compaction of tombstones.
- Concurrent read path.
- PDF extraction.
- Auth / access gate.
- Reranker or hybrid search.
- Streaming answer.
- Docker packaging and a public deployment.
- Same-host baseline against hnswlib/FAISS at matched recall.
- Quantization for memory-bound scale.

### Non-goals (do not build)

- Multi-tenant SaaS, billing, teams.
- Agents, tool calling, Graph RAG.
- Distributed or multi-node search.
- 3D map.
- KD-tree.
- Multi-metric benchmark.
- Static showcase site.
- Trace-replay page.
- Live benchmark endpoint.

These match `DECISIONS.md` D20. If one returns, it needs a new decision entry.

---

## 14. Risks and honest mitigations

| Risk | Why it matters | Mitigation |
|---|---|---|
| The merge is too big | Product + engine can become two unfinished projects | Build the engine first; keep the product to four tabs; obey D20 |
| Frontend carries old overclaims | Fake embeddings and "production-grade" language destroy trust | Delete `textToEmbedding`; rewrite README; no number without `bench.json` |
| Ollama is slow or unavailable | The demo fails on a stranger's machine | Health check; clear 503; document `ollama serve`; test with a fake client |
| SQLite and graph drift | Wrong citations or missing results | SQLite is the source of truth; rebuild on startup; persistence test |
| Startup rebuild is slow | Bad first-run experience | Measure it; target 10k chunks; show a loading state; graph snapshot is stretch |
| PCA map is misleading | Users read clusters as truth | Label it as a 2D projection; show sample size; no benchmark claims |
| Laptop benchmark noise | Numbers vary run to run | Three trials; record CPU/flags; report range; no cloud claim |
| Scope creep | The project never ships | D20 cut list; one active project; demo before extras |

---

## 15. Definition of done

v1 is done when all of these are true:

1. A user can upload or paste a document, see it listed, and delete it.
2. A restart preserves documents and search results.
3. A question returns an answer with citations to real retrieved chunks.
4. The map shows real embeddings in 2D, with the query and hits highlighted.
5. The Benchmark tab shows a real SIFT-100k report, its parameters, and its command.
6. `hns_tests` passes, including the two-cluster prune test and the restart test.
7. `sift10k` CI recall is at least 0.90 at the chosen configuration.
8. The README has the measured table, limitations, and a reproducible local setup.
9. The 60-second demo works without editing code or faking data.
10. The docs match the implementation; if they do not, fix the docs or the code.

If only the engine works, the project is a measured index without a product. If only
the product works, it is a RAG wrapper without depth. v1 is the pair.

---

## 16. Questions an interviewer can ask

- Why HNSW instead of brute force or a KD-tree?
- Why is the graph directed? What did you give up by not enforcing symmetry?
- Why mark-deleted instead of removing edges?
- Why rebuild the graph on startup instead of saving it?
- How do you know the answer key is correct?
- What does `efSearch` change, and what does it not change?
- Why is the benchmark a CLI and not an API?
- What happens when Ollama is down or slow?
- What happens if SQLite commits but the graph insert fails?
- What is the memory cost at 100k and at 1M?
- What would you change first for concurrent users?
- What did you deliberately cut, and why?

If the answer is "the docs say so," that is not an answer. The point of the project is
to be able to defend the tradeoffs, not to recite them.

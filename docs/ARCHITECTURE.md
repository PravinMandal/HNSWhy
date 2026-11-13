# HNSWhy — Architecture

> **Status: v1 implemented.** Engine, product backend, frontend, tests, sample
> knowledge base, and the frozen SIFT-100k benchmark report are in this repository.
> The decisions in `DECISIONS.md` remain the lock; this file now describes the
> shipped shape and notes where implementation details differ from the original
> plan.
>
> **v1 in one line:** a RAG app a stranger can use, backed by a from-scratch C++ HNSW
> engine, measured against exact search on SIFT-100k, with tests and a reproducible
> benchmark.

---

## 0. Ten-second version

A user uploads documents. The backend extracts text, splits it into chunks, embeds
each chunk with the configured provider, stores the text and vectors in SQLite, and
inserts the vectors into a from-scratch HNSW index. The default provider is an offline
deterministic hash embedder; Ollama and OpenAI-compatible endpoints are alternatives.
When the user asks a question, the backend embeds the question, retrieves the nearest
chunks with HNSW, drops those below the similarity floor, and asks the configured chat
provider to answer with the rest as numbered sources. The answer comes back with
citations when those sources support it, or as a clearly labelled general-knowledge
answer when they do not. A separate CLI measures the same engine on SIFT-100k and
writes `bench.json`; the UI's Benchmark tab reads that file.

The product is the demo. The engine is the depth. The benchmark is the evidence.

---

## 1. Why this shape

Three layers, three different jobs:

| Layer | What it is | Why it exists |
|---|---|---|
| **Product** | React UI, HTTP API, SQLite, providers | A stranger can use it without reading code |
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
        ┌───────▼────────┐              ┌──────────────────────▼─────────┐
        │ SQLite file    │              │ provider (optional network)    │
        │ source of truth│              │ hash / Ollama / OpenAI-compat. │
        └────────────────┘              └────────────────────────────────┘

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

The frontend was built from scratch with React 19, TypeScript, and Vite. It has no
dependency on the retired static showcase and no fake-embedding path. Hash routing
keeps the four sections linkable without a server-side router.

| File | Responsibility |
|---|---|
| `frontend/src/App.tsx` | mounts the application shell |
| `frontend/src/app/` | shell, hash router, theme/toast/data providers, sidebar, topbar |
| `frontend/src/api/client.ts` | typed HTTP client, upload progress, error normalization |
| `frontend/src/api/types.ts` | response types shared by the four features |
| `frontend/src/features/documents/` | upload/paste, list, search, detail, delete |
| `frontend/src/features/ask/` | question composer, cited answers, general-knowledge fallback, citations, timings |
| `frontend/src/features/map/` | 2D/3D map, focus, query overlay, inspector, projection hook |
| `frontend/src/features/benchmark/` | real report, recall chart, method table, latency, config |
| `frontend/src/lib/pca.ts`, `pca.worker.ts` | NIPALS PCA in a Web Worker with a synchronous fallback |
| `frontend/src/lib/` | document colours, formatting, unit tests |
| `frontend/src/ui/` | Button, Badge, Icon, Modal, PageHeader, states, toasts |
| `frontend/src/styles/` | tokens, base, layout, UI primitives, per-feature styles |

Rules for the frontend:

- No fake embeddings. Every point comes from `GET /api/map`.
- No benchmark numbers in the frontend. Numbers come from `bench.json` through the
  API.
- The map is labelled as a PCA projection, not the HNSW graph and not ground truth.
- The UI never calls Ollama or another provider directly. All model calls go through
  the backend.
- Every error state has a sentence a user can act on, plus a retry where retrying is
  meaningful.

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
| `product/app.{h,cpp}` | lifecycle, one mutex, startup rebuild, sample seeding, recovery |
| `product/document_store.{h,cpp}` | SQLite schema, transactions, CRUD, embeddings, metadata |
| `product/chunker.{h,cpp}` | text extraction (`pdftotext`), word-boundary chunks, overlap |
| `product/vector_index.{h,cpp}` | slots, chunk maps, incremental insert, tombstones, rebuild |
| `product/providers.{h,cpp}` | hash/extractive/OpenAI-compatible providers and factory |
| `product/ollama_client.{h,cpp}` | Ollama embed/generate adapter, timeouts, errors |
| `product/rag.{h,cpp}` | retrieve, build prompt, call chat, attach citations |

`DocumentStore` is the only code that writes SQL. It exposes typed methods, not a raw
database handle.

`OllamaClient` uses the Ollama HTTP API with bounded timeouts. Provider clients are
created per call, so a hung request cannot poison a shared connection. The same
abstraction supports OpenAI-compatible embedding and chat endpoints. The offline hash
and extractive providers are first-class implementations, not test doubles.

`RagService` is the product's retrieval story:

```text
question
  -> embed(question)
  -> normalize
  -> hnsw.search(query, k=5, ef=50)
  -> map slots to chunks
  -> drop chunks below min_source_similarity
  -> build prompt with numbered sources (or a no-sources instruction)
  -> chat provider generate(prompt)
  -> answer + citations + retrieved chunks + query vector + timings
```

Retrieval is additional context, not a gate. When the index is empty, the question
skips embedding and search but still goes to the chat provider with a no-sources
prompt. When chunks are found, any chunk below the cosine floor is omitted; the rest
become numbered sources. The system prompt tells the model to cite a source only when
that source directly supports the sentence, and to answer from general knowledge,
without citations, otherwise. The backend does not rewrite or reject model text; the
frontend labels an uncited answer as general knowledge. A provider failure returns a
`503` with a fixable message. The product never presents uncited model knowledge as
document evidence.

### 3.4 Engine library (`hnswhy_engine`)

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
    SearchResult search(std::span<const float> query, size_t k, size_t ef);
    void validate() const;

    size_t size() const;               // live vectors
    size_t slot_count() const;         // includes tombstones
    bool isDeleted(Slot id) const;

private:
    Slot greedySearchLayer(std::span<const float> query, Slot entry, int layer);
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

It links `hnswhy_engine` and never links SQLite, Ollama, or the HTTP server. A benchmark
run should work on a machine with no database and no model server.

### 3.6 Tests

`backend/tests/` builds `hns_tests`, linking `hnswhy_engine` (and, for integration tests,
the product layer). The test list and gates are in §11.

---

## 4. Data model and persistence

### 4.1 SQLite schema

```sql
CREATE TABLE documents (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    title      TEXT NOT NULL,
    source     TEXT NOT NULL,
    text       TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE chunks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
    ordinal     INTEGER NOT NULL,
    text        TEXT NOT NULL,
    created_at  TEXT NOT NULL
);

CREATE TABLE embeddings (
    chunk_id INTEGER PRIMARY KEY REFERENCES chunks(id) ON DELETE CASCADE,
    dim      INTEGER NOT NULL,
    vector   BLOB NOT NULL
);

CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE INDEX idx_chunks_document ON chunks(document_id, ordinal);
```

Open the connection with `PRAGMA foreign_keys = ON`; SQLite does not enforce the
cascade rules by default. Without it, deleting a document would leave orphan chunks and
embeddings.

The store also takes an exclusive advisory lock on `<db>.lock` for its lifetime.
Two server processes on one SQLite file would each keep a private in-memory index
and silently diverge, so the second opener fails fast. The lock descriptor is
`O_CLOEXEC` so a spawned `pdftotext` child cannot inherit and hold it.

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
  -> embed each chunk (configured provider)
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
rebuild repairs the graph. If an in-memory insert fails after commit, the request
returns an error and the application rebuilds the in-memory index from SQLite
immediately; the database remains the source of truth.

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
  the answer key for recall, tests, and benchmark ground truth. The product searches
  HNSW, not brute force as a second path. If a built graph's layer 0 is disconnected,
  `HNSW::search` scans exactly the live slots the graph walk cannot reach and merges
  them into the result, so recall does not silently collapse and the fallback cost
  grows with the stranded set, not with N. In that mode the layer-0 beam starts from
  both the last upper-layer node and `entry_`, because upper-layer descent can land in
  a different component. `stats.exact_fallbacks` counts queries that needed a fallback,
  not scans. The fallback is a repair path, not the normal path. The cached
  connectivity answer is invalidated after every structural change that can affect
  it — an insert that becomes the entry, has no live incoming edge, or follows a
  removed edge, and every deletion — so a stale "connected" flag could not skip the
  fallback.
- **Exception safety.** Public `insert()` checks its preconditions (the slot is in
  the store and the insert is append-only) before touching the graph. If the
  insertion body throws after that point, the graph is marked unusable and every
  later operation throws a `std::logic_error` telling the caller to rebuild. A bad
  caller argument does not poison a healthy graph.
- **HNSW.** Layered proximity graph. Layer 0 contains every node; upper layers are
  sparse shortcuts. Greedy descent above layer 0, beam search at layer 0.
- **Layer 0 storage.** `N * M0` `uint32_t` links plus a `uint16_t` degree per node.
  Pruning overwrites the node's fixed slots in order with `std::copy`; it does not
  swap-erase. No CSR.
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

1. User uploads `.pdf`/`.txt`/`.md` or pastes text.
2. Backend validates size and extracts plain text. PDFs go through `pdftotext`.
3. `Chunker` splits into ~350-word chunks with ~50-word overlap.
4. The configured embedding provider returns one vector per chunk. The default is
   the offline hash embedder; Ollama and OpenAI-compatible endpoints are supported.
5. Vectors are normalized.
6. `DocumentStore` commits the document, chunks, and embeddings in one transaction.
7. `VectorIndex` inserts each slot incrementally; the graph is not rebuilt.
8. UI shows the document, chunk count, and indexing status.

### 6.2 Ask

1. User asks a question.
2. Backend embeds and normalizes it, unless the library is empty.
3. `HNSW::search(question, k=5, ef=50)` returns slots.
4. Backend maps slots to live chunks.
5. Chunks below `min_source_similarity` (cosine) are omitted. With no live chunks or
   no chunk above the floor, the request still proceeds to generation with a
   no-sources prompt.
6. Build a prompt with the remaining numbered sources and instructions to cite only
   the sources that directly support the answer, otherwise answer from general
   knowledge without citations.
7. The configured chat provider returns the answer.
8. Backend returns `{ answer, sources, retrieved, timings }`.
9. UI renders clickable citations when present, or an explicit general-knowledge
   warning when the answer is uncited.

The answer is not trusted just because it is fluent. The citations are the product's
honesty mechanism: a user can open the chunk that supposedly supports the sentence.
When there are no citations, the UI says the answer came from the model's general
knowledge and should be verified; it never presents that answer as document-grounded.

### 6.3 Map

1. Backend samples live chunks and returns their vectors plus document metadata.
   The API default is 800 with a cap of 2,000; the frontend requests 1,200.
2. Frontend fits a three-component PCA with NIPALS power iteration, then normalizes
   the corpus coordinates into the viewport.
3. The user can switch between a 2D SVG view and a 3D canvas view, pan or orbit,
   zoom, focus a document, inspect a point, and overlay the last query.
4. Retrieved chunks are highlighted with query-coloured rings. The inspector shows
   chunk text, document, ordinal, PC coordinates, and whether the point was cited.

The map is a visual aid, not a benchmark and not a drawing of HNSW edges. If PCA looks
messy, that is allowed; the embeddings are real.

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
| `GET` | `/api/health` | — | status, database, store, embedding, and chat availability |
| `GET` | `/api/status` | — | provider/dimension, index counts, store totals, benchmark availability |
| `POST` | `/api/documents` | multipart `file` or JSON `{title,text}` | `{ document }` |
| `GET` | `/api/documents` | — | `[{ id, title, source, chunk_count, char_count, created_at }]` |
| `GET` | `/api/documents/{id}` | — | `{ document, text, chunks }` |
| `DELETE` | `/api/documents/{id}` | — | `{ deleted: true, id }` |
| `POST` | `/api/ask` | `{ question, k? }` | `{ answer, sources, query_vector, timings_ms, models }` |
| `GET` | `/api/map` | `?limit=800` | `{ total_chunks, sampled, points: [{ chunk_id, document_id, title, ordinal, preview, vector }] }` |
| `GET` | `/api/benchmark` | — | `{ available: true, report }` or `{ available: false, message, command }` |

Error shape:

```json
{
  "error": {
    "code": "PROVIDER_UNAVAILABLE",
    "message": "The configured embedding provider is not reachable."
  }
}
```

Validation rules:

- `question`: non-empty, max 2,000 characters.
- `k`: 1–20, default 5.
- document text: non-empty after trimming, max 4 MB per upload. The HTTP server
  accepts request payloads up to 25 MB, so the text limit is enforced by the
  application, not the transport.
- title: max 200 characters; omitted titles derive from the source name.
- map `limit`: 1–2,000, default 800.
- `DELETE /api/documents/{id}`: return `404` if the document does not exist; otherwise
  delete and return `{ "deleted": true }`.

There is no user auth in v1. The README must say the demo is for a local or trusted
environment, not a public multi-tenant service. If it is deployed publicly, put it
behind a simple access gate or accept that anyone who can reach it can upload and
delete documents. The transport-level guards below are not a substitute for auth;
they only stop a browser on another site from driving this API.

Transport guards, applied before routing:

- **Host allowlist.** A missing `Host` returns `400 MISSING_HOST`; a name outside
  the allowlist returns `403 FORBIDDEN_HOST`. The allowlist always contains
  `localhost`, `127.0.0.1`, and `::1`, plus the concrete bind host. Add LAN or
  reverse-proxy names with `HNSWHY_ALLOWED_HOSTS` (comma-separated) or a repeatable
  `--allowed-host`. This blocks DNS-rebinding pages that resolve their own name to
  loopback.
- **Cross-site writes.** `POST`/`PUT`/`PATCH`/`DELETE` reject a `Sec-Fetch-Site`
  that is not `same-origin` or `none`, and an `Origin` whose authority does not
  match the request `Host`, with `403 CROSS_SITE_REQUEST`.
- **Files.** The server sets `umask(0077)` and pre-creates the database at `0600`
  before taking the single-writer lock, so SQLite, WAL, SHM, temporary PDFs, and
  logs are owner-only.
- **PDF extraction.** At most two `pdftotext` children run at once (a 5 s wait
  returns `503 SERVER_BUSY`); each gets `ulimit -v`, `ulimit -t`, and `timeout`.

---

## 8. Benchmark and measurement

The benchmark is a separate CLI:

```bash
scripts/fetch_sift.sh   # once, if data/sift is missing
scripts/run_bench.sh    # builds release, computes ground truth, runs the sweep
```

The script invokes `build/backend/hns_gt` and `build/backend/hns_bench` with the
recorded parameters. The equivalent direct benchmark command is:

```bash
./build/backend/hns_bench \
  --base data/sift/sift_base.fvecs \
  --queries data/sift/sift_query.fvecs \
  --gt-ids data/sift100k/gt_ids.ivecs \
  --gt-dist data/sift100k/gt_dist.fvecs \
  --n 100000 --k 10 \
  --M 16 --ef-construction 200 \
  --ef 10,20,50,100,200 \
  --passes 3 --warmup 200 \
  --output docs/results/bench.json
```

The release report is committed under `docs/results/`; the dataset stays in
gitignored `data/`. A local scratch run may write `data/bench.json`.

Protocol:

1. Load and validate the base, queries, and ground truth.
2. Build the index once; record build time.
3. Warm up on 200 queries.
4. Run every `--ef-grid` point from the same build; for each point, run three timed
   passes over all 10,000 queries. `--ef` is the headline point.
5. Compute recall against the exact prefix ground truth.
6. Measure the exact-search baseline on the same queries, kernel, flags, and thread
   count, and store it as the `bruteforce` method in the same report.
7. Write `bench.json` with metrics, the curve, and metadata.

Metrics:

- `R@1`, `R@10`
- QPS
- p50 / p95 / p99 latency
- build time
- peak RSS
- dataset bytes and vector count (bytes per vector follows from the two)
- average distance computations per query
- exact-search QPS; the UI derives the speedup ratio from the reported QPS values
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
| Graph insert fails after DB commit | return error and rebuild the in-memory index from SQLite; the failed graph is poisoned until rebuilt |
| PDF extractor busy (two in flight) | `503 SERVER_BUSY`; the client retries |
| Benchmark file missing | Benchmark tab shows instructions, not a fake chart |
| Embedding dimension mismatch | reject the document with a clear error |
| Empty question | `400` with a validation message |
| Empty index | no retrieval; chat provider answers from general knowledge, marked uncited |
| Sources below the similarity floor | omit them; chat provider answers from general knowledge, marked uncited |
| Uncited model answer | returned with its sources; UI labels it general knowledge |

The server must not crash on a bad request, a missing file, or a model error. Those
are normal product states.

---

## 10. File layout and build targets

```text
HNSWhy/                          # repo root
├── CMakeLists.txt                 # adds backend/
├── CMakePresets.json              # dev (ASan/UBSan), release, release-native, ci
├── frontend/
│   ├── package.json
│   ├── index.html
│   ├── vite.config.ts
│   ├── tsconfig.json
│   └── src/
│       ├── App.tsx
│       ├── main.tsx
│       ├── components/            # layout, modals, scatters, answer view, icons
│       ├── context/               # app data and theme providers
│       ├── lib/                   # api, PCA, colors, format, router, types, tests
│       ├── pages/                 # Documents, Ask, Map, Benchmark
│       └── styles/                # tokens, base, components, pages
│
├── backend/
│   ├── CMakeLists.txt
│   ├── third_party/               # httplib.h, json.hpp
│   ├── src/
│   │   ├── main.cpp               # bootstrap only
│   │   ├── api/routes.{h,cpp}
│   │   ├── engine/                # vector_store, brute_force, hnsw, metrics
│   │   └── product/
│   │       ├── app.{h,cpp}        # lifecycle, mutex, seeding, recovery
│   │       ├── config.{h,cpp}
│   │       ├── document_store.{h,cpp}
│   │       ├── chunker.{h,cpp}
│   │       ├── vector_index.{h,cpp}
│   │       ├── providers.{h,cpp}
│   │       ├── ollama_client.{h,cpp}
│   │       ├── http.{h,cpp}
│   │       ├── rag.{h,cpp}
│   │       ├── text.{h,cpp}
│   │       └── errors.h
│   ├── bench/                     # hns_bench, hns_gt, fvecs, report
│   └── tests/
│       ├── framework/             # tiny test harness
│       ├── unit/                  # engine, store, chunker, providers, RAG, persistence
│       └── integration/           # API, siftsmall, benchmark
│
├── samples/                       # seeded only into a fresh database
├── scripts/
│   ├── fetch_sift.sh
│   ├── run_bench.sh
│   └── run_server.sh
├── data/                          # gitignored datasets and local DB
├── docs/
│   └── results/
│       ├── README.md
│       └── bench.json             # frozen release artifact
├── README.md
└── .gitignore
```

The root `CMakeLists.txt` only calls `add_subdirectory(backend)`; targets live in
`backend/CMakeLists.txt`. Presets keep builds under `build/` (`build/` for release,
`build/dev/` for sanitizers), matching the README commands.

### CMake targets

```cmake
add_library(hnswhy_engine STATIC ...)      # no SQLite, HTTP, JSON, or provider code
add_library(hnswhy_product STATIC ...)     # app, store, index, providers, RAG, routes
add_library(hnswhy_io STATIC ...)          # fvecs/ivecs and ground truth
add_executable(hnswhy_server src/main.cpp)
add_executable(hns_bench bench/main.cpp bench/report.cpp)
add_executable(hns_gt bench/gt_main.cpp)
add_executable(hns_tests ...)          # links hnswhy_engine, hnswhy_io, hnswhy_product
```

`hns_tests` links the same product and engine libraries as the server; there is no
second implementation. The engine library does not know about documents, SQL, HTTP, or
providers. Dependencies: C++20, CMake, `cpp-httplib` (header-only),
`nlohmann/json` (header-only), SQLite3, and optionally OpenSSL for HTTPS provider
URLs. Ollama is optional at runtime.

---

## 11. Testing

### 11.1 Engine tests

| Test | What it proves |
|---|---|
| `test_distance` | squared L2, normalization, and invalid-dimension handling |
| `test_vector_store` | add/get/reserve; dimension checks; contiguous storage |
| `test_brute_force` | exact top-k; ties deterministic by slot |
| `test_hnsw` | insert/search, insertion orders, duplicates, empty graphs, stale-connectivity regression, exception poisoning, invariants |
| `test_prune` | two-cluster diversity test; nearest-only disconnects, heuristic keeps a bridge |
| `test_delete` | tombstones never returned; traversal survives; entry/top-layer safety |
| `test_metrics` | recall and latency percentiles |
| `test_framework` | `CHECK_NEAR` rejects NaN and infinity |

### 11.2 Product tests

| Test | What it proves |
|---|---|
| `test_chunker` | word-boundary chunks, overlap, short input |
| `test_document_store` | schema, transactions, cascade deletes, metadata, sampling, batched chunk lookup |
| `test_providers` | hash, extractive, Ollama/OpenAI request and error paths, index range checks |
| `test_vector_index` | slot mapping, incremental insert, tombstone delete, rebuild |
| `test_rag` | retrieval, similarity floor, citation-first prompt, citations, general-knowledge fallback |
| `test_persistence` | insert -> restart -> rebuild -> same results; sample seeded once; skipped seed retried; concurrent ask/upload |
| `test_api` | upload/ask/map/delete flow, multipart, PDF, 415 validation, Host/Origin guards, static traversal, errors |
| `test_bench` | fvecs/ivecs parsing, report schema |
| `test_cli` | benchmark numeric parsing rejects signs, junk, and overflow |
| `test_config` | M0 derivation, environment overrides, API-key scoping |

### 11.3 CI and sanitizers

`.github/workflows/ci.yml` configures the `ci` preset, builds all targets, and runs
`ctest`; a second job runs `npm ci`, `npm test`, and `npm run build` for the frontend.
The committed sift10k fixture gate always runs. Siftsmall integration tests skip
unless `HNSWHY_SIFTSMALL_DIR` points at a local dataset. Release verification on this
machine runs the same suite with `HNSWHY_SIFTSMALL_DIR="$PWD/data/siftsmall"`:
**168/168 pass**. The frontend suite is **67 tests in 17 files**.

The `dev` preset enables ASan and UBSan. The full suite is sanitizer-clean.

### 11.4 End-to-end

`scripts/e2e/run.sh` builds if needed, starts the real server on a fresh SQLite
database, runs `api-e2e.sh` (26 HTTP checks), then runs `ui-e2e.cjs` (16 browser
checks) and stops the server. The browser suite uses the built `frontend/dist`
bundle, not the Vite dev server, and asserts that the production stylesheet is
loaded. Screenshots and logs are written under `$OUT`; the committed evidence is
in `docs/results/e2e/`.

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
| Product | PDF/TXT/Markdown upload, paste, list, detail, delete, ask, citations, 2D/3D map, benchmark tab |
| Persistence | SQLite source of truth; incremental insert; tombstones; rebuild on startup |
| Engine | flat store, squared L2, brute force, HNSW insert/search/prune, mark-deleted |
| Evidence | SIFT-100k, 10k queries, 3 passes per curve point, `bench.json`, README table |
| Safety | 168 tests, two-cluster test, siftsmall recall gate, persistence test, ASan/UBSan, 26 API + 16 browser E2E checks |
| Ops | documented local run, `.env.example`, sample KB, no auth |

### Stretch (after v1)

- One SIFT-1M configuration.
- Graph snapshot with a corruption test.
- Physical compaction of tombstones.
- Concurrent read path.
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
| Scope creep | Product + engine can become two unfinished projects | Keep the product to four tabs; obey D20 |
| Frontend overclaims | Fake embeddings and "production-grade" language destroy trust | Every map point comes from the API; no number without `bench.json` |
| Ollama is slow or unavailable | The demo fails on a stranger's machine | Offline defaults; health check; clear 503; document `ollama serve` |
| SQLite and graph drift | Wrong citations or missing results | SQLite is the source of truth; rebuild on startup; persistence test |
| Startup rebuild is slow | Bad first-run experience | Measure it; target 10k chunks; show a loading state; graph snapshot is stretch |
| PCA map is misleading | Users read clusters as truth | Label the source and projected dimensions; show sample size; no benchmark claims |
| Laptop benchmark noise | Numbers vary run to run | Three trials; record CPU/flags; report range; no cloud claim |
| Scope creep | The project never ships | D20 cut list; one active project; demo before extras |

---

## 15. Definition of done

v1 is done when all of these are true:

1. A user can upload or paste a document, see it listed, and delete it.
2. A restart preserves documents and search results.
3. A question returns an answer with citations to real retrieved chunks.
4. The map shows real embeddings in 2D and 3D, with the query and hits highlighted.
5. The Benchmark tab shows a real SIFT-100k report, its parameters, and its command.
6. `hns_tests` passes, including the two-cluster prune test and the restart test.
7. The siftsmall recall gate passes at the chosen configuration (R@1 0.99, R@10 0.997).
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

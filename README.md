# HNSWhy

[![CI](https://github.com/PravinMandal/HNSWhy/actions/workflows/ci.yml/badge.svg)](https://github.com/PravinMandal/HNSWhy/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A retrieval-augmented question answering app backed by a from-scratch C++20 HNSW
vector engine.

> **Naming:** **HNSWhy** is both the shipped product and the from-scratch engine
> inside it. One name, one repository.

> **Status: v1 shipped.** Engine, product backend, React frontend, sample knowledge
> base, sanitizer-clean test suite, and a frozen SIFT-100k benchmark report are all
> in this repository. The numbers below come from `docs/results/bench.json`.

---

## What it is

HNSWhy lets a user upload PDF, Markdown, or plain-text documents, ask questions,
and get answers with citations. At its core is a from-scratch HNSW engine: a flat
vector store, exact brute force, a layered proximity graph, tombstones, a real
benchmark, and tests that exercise every layer.

The point is not "another RAG wrapper" and not "another HNSW implementation." The
point is the pair:

- a product a stranger can use;
- an engine whose quality is measured, not asserted.

> HNSWhy answers questions over uploaded documents using a from-scratch HNSW
> index. On 100,000 SIFT vectors, the same engine found **98.4%** of the true top-10 at
> **7,281** queries per second, while exact search did **200.9** QPS — a measured
> **36.2×** throughput improvement at that recall.

The query is embedded with the same provider as the corpus. The retrieved chunks are
loaded from SQLite, passed to the chat provider as numbered sources, and returned with
citations. A separate CLI measures the same engine library, so product latency and
benchmark claims cannot drift apart.

---

## Demo

| Ask with citations | 2D embedding map |
|---|---|
| ![Ask tab showing a cited answer and retrieved chunks](docs/results/e2e/ask-answer.png) | ![2D PCA projection of the sample corpus](docs/results/e2e/map-2d.png) |

The screenshots come from the automated browser suite against the production
build. More evidence is in [`docs/results/e2e/`](docs/results/e2e/).

---

## Results

SIFT-100k, 10,000 queries, 128 dimensions, `M=16`, `M0=32`,
`efConstruction=200`. Three timed passes per point after 200 warmup queries.

| Dataset | N | Queries | R@1 | R@10 | QPS | p50 | p95 | p99 | Build | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT-100k, `efSearch=50` | 100,000 | 10,000 | 0.9927 | 0.9836 | 7,281 | 0.142 ms | 0.173 ms | 0.184 ms | 33.4 s | 93.1 MB |
| SIFT-100k, exact | 100,000 | 10,000 | 1.0000 | 1.0000 | 200.9 | 4.911 ms | 5.245 ms | 5.854 ms | — | — |

Full sweep:

| efSearch | R@1 | R@10 | QPS | p50 ms | p95 ms |
|---:|---:|---:|---:|---:|---:|
| 10 | 0.8779 | 0.7989 | 22,026 | 0.045 | 0.063 |
| 20 | 0.9558 | 0.9123 | 14,387 | 0.070 | 0.090 |
| 50 | 0.9927 | 0.9836 | 7,281 | 0.142 | 0.173 |
| 100 | 0.9985 | 0.9966 | 4,177 | 0.247 | 0.305 |
| 200 | 0.9998 | 0.9996 | 2,435 | 0.424 | 0.530 |
| exact | 1.0000 | 1.0000 | 200.9 | 4.911 | 5.245 |

Environment: GCC 16.2.1, `Release -O3`, 12th Gen Intel Core i5-12450H, 12 hardware
threads, Linux 7.2.11, AVX2 disabled for this run. These are local measurements, not
cloud numbers. The browser formats the stored report and derives ratios such as
speedup from its reported QPS values; it never re-measures or recomputes recall.
When the code or benchmark changes, rerun `scripts/run_bench.sh` and replace
`docs/results/bench.json`; do not edit values by hand.

---

## Product

| Section | What it does |
|---|---|
| **Documents** | Upload PDF/TXT/Markdown or paste text; inspect extracted chunks; delete. |
| **Ask** | Grounded answers with numbered citations, retrieval scores, timings, and model metadata; the query vector feeds the Map overlay. |
| **Map** | Actual embedding-space visualization in 2D and 3D, with PCA metadata, document focus, point inspection, and retrieval highlights. |
| **Benchmark** | Real SIFT-100k report: recall curve, latency percentiles, method table, exact comparison, config and environment. |

The UI supports light and dark themes, keyboard focus states, loading/empty/error
states, and responsive layouts down to narrow mobile screens.

The default configuration uses an offline **feature-hashing embedder**
(`local-hash-v1`, 256 dimensions) and an offline **extractive answerer**
(`local-extractive-v1`). It makes the demo work without Ollama or an API key. Both
providers are independently configurable:

- **Embeddings:** `hash`, `ollama`, or any OpenAI-compatible endpoint.
- **Chat:** `extractive`, `ollama`, or any OpenAI-compatible endpoint.

The active provider, model, and dimension are stored in SQLite. Switching embedding
space requires an explicit `--reembed` run; the server refuses to mix incompatible
vectors.

---

## Architecture

```text
React UI (Vite + TypeScript)
  ├── Documents      upload/paste, list, detail, delete
  ├── Ask            question -> grounded answer + citations
  ├── Map            2D/3D PCA of real chunk embeddings
  └── Benchmark      reads the frozen bench.json report
        │
        ▼
C++ HTTP server (cpp-httplib)
  ├── api/routes       validation, JSON, multipart, static files
  ├── product/app      lifecycle, mutex, seeding, recovery
  ├── product/store    SQLite: documents, chunks, embeddings, metadata
  ├── product/index    VectorStore + HNSW + slot/tombstone maps
  ├── product/providers hash, Ollama, OpenAI-compatible
  └── product/rag      retrieval, grounded prompt, citations
        │
        ▼
hnswhy_engine (static library)
  VectorStore · BruteForce · HNSW · metrics
```

- **SQLite is the source of truth.** Documents, chunks, and embeddings live there.
  The HNSW graph is an in-memory acceleration structure rebuilt at startup.
- **Insertion is incremental.** Uploads add vectors without rebuilding the graph.
  Deletes mark slots as tombstones; tombstones stay traversable but never appear in
  results.
- **One mutex serializes document, index, and search operations.** Provider HTTP
  clients are created per call. An exclusive `flock` on the database file refuses a
  second server process, so two in-memory indexes cannot silently diverge.
- **The benchmark is a CLI.** It never runs inside an HTTP request, so network and
  JSON latency do not contaminate the engine numbers.
- **The engine is a library.** Server, benchmark, and tests link the same code.

The full component map, data model, API, and memory budget are in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## Setup

Prerequisites:

- C++20 compiler (GCC 13+ or Clang 16+), CMake ≥ 3.25 (the presets use schema v6), Ninja
- SQLite3 development headers
- Node 20+ and npm
- `pdftotext` (from Poppler) for PDF uploads
- Ollama only if you choose a local model provider

```bash
# 1. Build the engine, server, benchmark, and tests
cmake --preset release
cmake --build --preset release

# 2. Run the test suite
./build/backend/hns_tests

# 3. Build the frontend (the server serves frontend/dist)
cd frontend
npm install
npm run build
cd ..

# 4. Start the product
./build/backend/hnswhy_server --db data/hnswhy.db --port 8080
```

Open <http://127.0.0.1:8080>. On a fresh database with the offline hash embedder
(the default), the server seeds the sample knowledge base from `samples/`. It never
overwrites an existing library. Use `--no-sample` to start empty.

The server accepts loopback `Host` names and rejects anything else with
`403 FORBIDDEN_HOST`. If you bind to a LAN address or put it behind a proxy, add the
names clients use with `HNSWHY_ALLOWED_HOSTS=hnswhy.example,192.168.1.20` or a
repeatable `--allowed-host` flag. State-changing requests from another origin are
also rejected. These are transport guards, not authentication.

For frontend development, run the API on port 8080 and Vite separately:

```bash
cd frontend && npm run dev   # http://127.0.0.1:5173, proxies /api to :8080
```

Configuration is environment-first, command-line-second. Copy `.env.example` to
`.env` and edit it, then use `scripts/run_server.sh`:

```bash
scripts/run_server.sh --port 8080
```

API keys are read from the environment or `.env`; they are never hardcoded, logged,
or stored in SQLite. `.env` is gitignored.

### Ollama example

```bash
ollama serve
ollama pull nomic-embed-text
ollama pull llama3.2

HNSWHY_EMBEDDING_PROVIDER=ollama \
HNSWHY_EMBEDDING_MODEL=nomic-embed-text \
HNSWHY_CHAT_PROVIDER=ollama \
HNSWHY_CHAT_MODEL=llama3.2 \
scripts/run_server.sh --reembed
```

`--reembed` replaces existing vectors with the new embedding space. Omit it when the
stored metadata already matches the configured provider.

---

## API

All endpoints are under `/api` and return JSON; uploads accept multipart form data or
a JSON body. Errors use one shape: `{"error": {"code": "...", "message": "..."}}`.

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/health` | Liveness |
| `GET` | `/api/status` | Provider, dimension, index, store, and benchmark state |
| `GET` | `/api/documents` | List documents |
| `GET` | `/api/documents/{id}` | Document detail and chunks |
| `POST` | `/api/documents` | Upload a file or paste text |
| `DELETE` | `/api/documents/{id}` | Delete a document and tombstone its vectors |
| `POST` | `/api/ask` | Answer a question with cited sources |
| `GET` | `/api/map` | Sampled chunk vectors for the embedding map |
| `GET` | `/api/benchmark` | Frozen benchmark report or an unavailable message |

Static frontend files are served from the same process. API routes are matched
before the static mount; client routing uses URL hashes, so no server-side
fallback is required.

---

## Tests

```bash
# Release suite (166 tests; the committed sift10k fixture always runs,
# siftsmall runs when the dataset is present)
HNSWHY_SIFTSMALL_DIR="$PWD/data/siftsmall" ./build/backend/hns_tests

# ASan + UBSan suite
cmake --preset dev
cmake --build --preset dev
HNSWHY_SIFTSMALL_DIR="$PWD/data/siftsmall" ./build/dev/backend/hns_tests

# Frontend unit tests (17 files, 67 tests)
cd frontend && npm test

# End-to-end: fresh database, real server, real browser
scripts/e2e/run.sh
```

Coverage includes squared-L2 distance, `VectorStore` invariants, exact search,
HNSW insertion orders, duplicate vectors, pruning, deletion/tombstones, restart
persistence, chunking, provider behavior, RAG citations, the HTTP API (including
Host/Origin guards and static-path traversal), report schema, and real siftsmall
recall. The frontend tests cover routing, the data provider,
theme persistence, dialogs, formatting, PCA, and page behaviour. The end-to-end
suite runs 26 API checks and 16 browser checks against a fresh database; it also
asserts that the production stylesheet is actually applied, so a broken static
asset cannot pass as a working page.

---

## Reproduce the benchmark

```bash
scripts/fetch_sift.sh   # once, if data/sift is missing
scripts/run_bench.sh    # builds release, computes ground truth, runs the sweep
```

The script writes `docs/results/bench.json`. Raw SIFT data and generated ground truth
stay in gitignored `data/`. The server only reads the report; it never runs the
benchmark during a request.

---

## What is deliberately not here

- A wrapper around FAISS, hnswlib, usearch, or another ANN library. The graph is
  written from scratch.
- A claim of novelty. HNSW is a published algorithm; this is an independent
  implementation and evaluation.
- A 1M sweep. 1M is one optional configuration after 100k is solid.
- A KD-tree. It is a poor fit at 128/768 dimensions and would be decoration.
- A multi-metric benchmark or a cosine-specific index. Product embeddings are
  normalized; L2 ranking equals cosine ranking.
- Auth, multi-user teams, billing, agents, Graph RAG, or distributed search.
- A graph snapshot. SQLite is the source of truth; the graph rebuilds on startup.
- A public multi-tenant deployment. This is a portfolio-grade v1, not a hardened
  service.

---

## Limitations

- A single application mutex serializes uploads, deletes, and searches.
- The graph is rebuilt from SQLite on startup; startup time grows with the corpus.
- Deletion leaves tombstones in memory; physical compaction is deferred.
- The benchmark is SIFT-100k, not 1M.
- Squared L2 only; product embeddings are normalized so L2 ranking equals cosine.
- No auth. Run it locally or behind a trusted access gate. Host allowlisting and
  cross-site write rejection stop browser-driven misuse, not a direct client.
- Uploaded text is trusted input; prompt-injection hardening is out of scope.
- Benchmark numbers are local to the recorded machine, not cloud guarantees.

---

## Documents

| File | What it is |
|---|---|
| [`docs/START-HERE.md`](docs/START-HERE.md) | the on-ramp: product + engine, scope, dataset ladder, definition of done |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | the locked choices and why |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | components, data model, API, persistence, memory, tests |
| [`docs/HNSW-SPEC.md`](docs/HNSW-SPEC.md) | the algorithm contract, directed graph, prune example, tombstones |
| [`docs/plan.md`](docs/plan.md) | the deep product/algorithm/measurement reference |
| [`docs/PROGRESS.md`](docs/PROGRESS.md) | persistent implementation state and phase log |
| [`docs/results/README.md`](docs/results/README.md) | the frozen `bench.json` release artifact and how it is produced |
| [`docs/results/tests/`](docs/results/tests/) | raw release, sanitizer, CI, frontend, and E2E evidence |
| [`docs/reviews/final-iteration-2026-11-11.md`](docs/reviews/final-iteration-2026-11-11.md) | five-critic review, findings, and fix verification |
| [`SECURITY.md`](SECURITY.md) | local threat model and vulnerability reporting |
| [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) | vendored library licenses |

If two docs disagree, `DECISIONS.md` wins. If a number appears in a doc without a
`bench.json` entry, it is stale; replace it or delete it.

---

## License

MIT. See [`LICENSE`](LICENSE).

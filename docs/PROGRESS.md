# HNSWhy — Implementation Progress

> Persistent state for resuming work. Updated at the end of every phase.
> Vision: `docs/goal.md`. Decisions: `docs/DECISIONS.md`. Architecture reference:
> `docs/ARCHITECTURE.md`.

## Status

| Phase | Scope | State |
|---|---|---|
| 0 | Repository skeleton and empty file inventory | done |
| 1 | Build system + engine core (distance, VectorStore, BruteForce) + tests | done |
| 2 | fvecs/ivecs loader, ground-truth tool, siftsmall gate | done |
| 3 | HNSW (insert/search/prune/tombstones/validate) + tests | done |
| 4 | Benchmark CLI, SIFT-100k ground truth, real `bench.json` | done |
| 5 | Product backend: SQLite, chunking, providers, RAG, HTTP API | done |
| 6 | Frontend from scratch: Documents, Ask, Map (2D/3D), Benchmark | done |
| 7 | Sample knowledge base, end-to-end tests, critics, docs, release | done |
| 8 | Correctness hardening and automated API + browser E2E | done |

## Environment (verified)

- GCC 16.2.1, Clang 23.1.3, CMake 4.4.4, Ninja 1.13.2
- SQLite 3.54.0 with development headers
- Node 26.13.0 / npm 11.20.0
- `pdftotext` available (PDF extraction)
- Ollama is not installed; offline providers are the default and Ollama paths are
  configurable and covered by provider tests
- SIFT and siftsmall datasets under `data/`
- CPU: 12th Gen Intel Core i5-12450H, 12 hardware threads

## Decisions taken during implementation

- D21: default offline embedding provider is a deterministic hashed
  bag-of-words embedder (`local-hash-v1`, 256 dims), used only when no external
  provider is configured. It makes the sample KB and tests work without Ollama.
- D22: provider, model and dimension are stored in SQLite; a mismatch refuses
  startup with a clear re-embed path instead of mixing embedding spaces.
- D23: PDF extraction shells out to `pdftotext` (available on the target
  machine); no PDF parsing code is vendored.
- D24: the frontend is built from scratch (React + Vite), not imported from an
  earlier prototype.
- D25: HNSW tombstones stay in the graph and remain traversable. Queries filter
  them from results; insertion beams include them, so a new node can attach
  even after every previous node was deleted.
- D26: the diversity prune keeps exact duplicates (`d(c, s) == 0`) up to the
  degree cap. Rejecting them turned copies into one-way leaves, so a query could
  not reach all duplicates and returned fewer than k results.
- D27: benchmark ground truth for a base prefix is recomputed with our exact
  BruteForce; the shipped SIFT-1M answer key is not valid for a 100k prefix.
  The benchmark verifies the exact baseline against that ground truth before
  reporting HNSW numbers.
- D28: the Map tab projects real sampled embeddings with a client-side NIPALS
  PCA. It is a visualization of the embedding space, not a drawing of HNSW edges.
- D29: the sample knowledge base has eight Markdown documents (16 chunks) and is
  seeded only into a fresh database with the offline hash embedder.

## Test and verification state

- Release suite: **143/143 passed** with `HNSWHY_SIFTSMALL_DIR` set.
- ASan/UBSan suite: rerun after the final hardening pass; no sanitizer reports.
- Frontend: `tsc --noEmit` clean; **55/55 Vitest tests in 15 files passed**;
  `npm run build` succeeds.
- API end-to-end: **25/25 checks passed** against a fresh database and the real
  server binary. The suite covers read endpoints, ask, map, benchmark, JSON and
  multipart uploads, PDF extraction, 400/404/415 paths, deletes, and the
  single-writer lock.
- Browser end-to-end: **16/16 checks passed** in headless Chromium against the
  built frontend. The suite covers Documents, Ask, Map, Benchmark, theme
  persistence, mobile layout and drawer, skip link, focus trapping, console and
  network errors, and asserts that the production stylesheet is applied.
- Evidence and screenshots: `docs/results/e2e/`; raw run log:
  `docs/results/e2e/run-2026-11-11.log`.
- Siftsmall gate: R@1 0.99, R@10 0.997 with the default HNSW configuration.
- Live smoke test on the sample library: ask with citations, 2D/3D map, and
  benchmark views are now covered by the automated browser suite instead of a
  manual check.

## Log

### Phase 0 — skeleton
Created empty backend file inventory; no code. Removed twice by request, final
inventory approved in `docs/goal.md` flow.

### Phase 1 — build + engine core (done)
CMake presets (`dev` with ASan/UBSan, `release`, `release-native`, `ci`),
`VectorStore` (flat contiguous storage), squared L2, `BruteForce` exact search,
custom test harness. Tests cover distances, store invariants, heap ordering,
ties, k caps, and dimension errors.

### Phase 2 — data I/O + exact gate (done)
Strict fvecs/ivecs readers/writers, direct-to-store prefix loading, ground-truth
tool (`hns_gt`). The siftsmall integration test reproduces the shipped answer
key exactly: tie-aware recall 1, ID overlap 1, max k-th distance error 0.

### Phase 3 — HNSW (done)
Implemented `HNSW` with flat layer-0 adjacency, sparse upper layers, geometric
levels, greedy upper descent, layer-0 beam search, diversity pruning (with
nearest-only ablation), mark-delete tombstones, structural `validate()`, and
search statistics. Tests cover empty graphs, duplicate vectors, deterministic
seeds, insertion-order permutation, `ef < k`, prune rules, deletion of the
entry point, delete-all then insert, and metrics.

Real-data gate on siftsmall (10k x 128, 100 queries, M=16, M0=32,
efConstruction=200, efSearch=50): **R@1 0.99, R@10 0.997** with roughly 1.3–1.7 s
build, p50 0.05–0.06 ms, and p95 0.08–0.09 ms across local runs.

Two real bugs were found only by the larger data: a back-edge written into a
full flat row corrupted the next node's adjacency, and duplicate vectors formed
unreachable one-way leaves. Both are fixed and covered by tests.

### Phase 4 — benchmark (done)
`hns_bench` loads a base prefix, queries and exact ground truth, builds HNSW
once, sweeps efSearch with warmup and repeated timed passes, measures exact
BruteForce as the baseline, and writes a structured JSON report with dataset,
config, build time, RSS, recall, QPS, latency percentiles, distance counts,
compiler and hardware.

SIFT-100k ground truth (10k queries, top 100) was generated with `hns_gt`.
Release run: build 36.1 s, peak RSS 92.5 MB. At `efSearch=50`,
**R@1 0.9926, R@10 0.9839, 6,895 QPS**, p95 0.184 ms, versus exact search at
193.5 QPS — a 35.6x speedup at that recall. The exact baseline matched the
ground-truth distances. `docs/results/bench.json` holds the full curve.

### Phase 5 — product backend (done)
SQLite schema with documents, chunks, embeddings, and metadata; stable
autoincrement IDs; cascade deletes; WAL and foreign keys. Chunking with
word-boundary splitting and overlap. Offline hash embedding and extractive chat
providers, plus Ollama and OpenAI-compatible HTTP clients. Embedding-space
metadata and `--reembed`. Incremental `VectorIndex` with slot maps, tombstones,
and guarded rebuilds. Grounded RAG prompt, numbered citations, refusal path.
HTTP API for health, status, documents, ask, map, and benchmark, with static
frontend serving and a consistent error shape. One application mutex serializes
shared state; provider clients are per call.

Implementation lives in `backend/src/product/` and `backend/src/api/`; the
engine remains independent in `backend/src/engine/`.

### Phase 6 — frontend (done)
Built from scratch with React 19, TypeScript, and Vite. Four sections:
Documents (upload/paste/detail/delete), Ask (composer, answer, sources,
timings, model metadata), Map (2D SVG and 3D canvas PCA projections with pan,
zoom, orbit, document focus, point inspection, query overlay, retrieval
highlights), and Benchmark (real report only). Design tokens for light/dark,
Inter/JetBrains Mono, custom icons, responsive layouts, keyboard focus, and
loading/empty/error states. Frontend is served from `frontend/dist` by the C++
server in production.

### Phase 7 — release (done)
Sample KB expanded to eight documents / sixteen chunks. Frontend unit tests
added for PCA and formatting. Responsive layouts verified at 390, 768, and 1440
pixels. Map contrast and point sizing tuned for sparse and dense maps. README,
architecture notes, progress log, `.env.example`, and run scripts updated to the
shipped code. Full release and sanitizer suites pass.

### Phase 8 — correctness hardening and automated E2E (done)
A full review pass found and fixed issues that the happy path did not expose:

- **HNSW stale connectivity.** An incremental insert could prune an edge or
  attach a node to a tombstoned component while the cached layer-0
  "connected" flag stayed true. Search then skipped the exact fallback and
  returned a wrong neighbour. Every successful insert now marks the cache
  dirty; a deterministic regression test reproduces the old failure on seed
  6942.
- **Single-writer database lock.** Two server processes on one SQLite file each
  kept their own in-memory index. `DocumentStore` now takes an exclusive
  `flock` on `<db>.lock` and refuses a second opener. The descriptor is
  `O_CLOEXEC` so spawned `pdftotext` cannot inherit it.
- **API media types.** `/api/ask` and JSON uploads now return 415 for a
  non-JSON `Content-Type` instead of trying to parse the body as JSON.
- **Benchmark input validation.** Numeric flags reject signs, whitespace,
  overflow, and `--ef 0`; the ground-truth cache is stamped with the base,
  query, prefix, and `k` hashes so a changed input cannot silently reuse a
  stale answer key.
- **Startup order.** The server binds its port before opening SQLite and
  rebuilding the index, so a busy port fails fast.
- **Re-embed path.** `--reembed` no longer builds the HNSW graph twice.
- **Empty exact search.** `BruteForce` returns an empty result for an empty
  store instead of throwing a dimension error.
- **Test harness.** `CHECK_NEAR` fails on NaN and infinity.
- **Test isolation.** The benchmark-report persistence test now releases the
  single-writer lock before opening the same database again.

The new `scripts/e2e/run.sh` starts a fresh server, runs 25 API checks and 16
browser checks, writes screenshots, and stops the server. `ui-e2e.cjs` asserts
that the production stylesheet is applied, so a missing CSS asset cannot pass
as a working page.

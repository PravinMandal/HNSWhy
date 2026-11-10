# HNSWhy / VectorForge — Implementation Progress

> Persistent state for resuming work. Updated at the end of every phase.
> Vision: `docs/goal.md`. Decisions: `docs/DECISIONS.md`. Architecture reference:
> `docs/ARCHITECTURE.md` (being revised as code lands).

## Status

| Phase | Scope | State |
|---|---|---|
| 0 | Repository skeleton and empty file inventory | done |
| 1 | Build system + engine core (distance, VectorStore, BruteForce) + tests | done |
| 2 | fvecs/ivecs loader, ground-truth tool, siftsmall gate | done |
| 3 | HNSW (insert/search/prune/tombstones/validate) + tests | done |
| 4 | Benchmark CLI, SIFT-100k ground truth, real `bench.json` | done |
| 5 | Product backend: SQLite, chunking, providers, RAG, HTTP API | in progress |
| 6 | Frontend from scratch: Documents, Ask, Map (2D/3D), Benchmark | pending |
| 7 | Sample knowledge base, end-to-end tests, critics, docs, release | pending |

## Environment (verified)

- GCC 16.2.1, Clang 23.1.3, CMake 4.4.4, Ninja 1.13.2
- SQLite 3.54.0 with development headers
- Node 26.13.0 / npm 11.20.0
- `pdftotext` available (PDF extraction)
- Ollama is **not installed**; providers must degrade cleanly and be configurable
- SIFT and siftsmall datasets already downloaded under `data/`
- CPU: 12th Gen Intel Core i5-12450H, 12 hardware threads

## Decisions taken during implementation

- D21: default offline embedding provider is a deterministic hashed
  bag-of-words embedder (`local-hash-v1`, 256 dims), used only when no external
  provider is configured. It makes the sample KB and tests work without Ollama.
- D22: provider, model and dimension are stored in SQLite; a mismatch refuses
  startup with a clear re-embed path instead of mixing embedding spaces.
- D23: PDF extraction shells out to `pdftotext` (available on the target
  machine); no PDF parsing code is vendored.
- D24: the frontend is built from scratch (React + Vite), not imported from the
  old VectorForge project.
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
efConstruction=200, efSearch=50): build ~1.38 s, **R@1 0.99, R@10 0.997**,
p50 ~0.046 ms, p95 ~0.07 ms. Full suite is ASan/UBSan clean.

Two real bugs were found only by the larger data: a back-edge written into a
full flat row corrupted the next node's adjacency, and duplicate vectors formed
unreachable one-way leaves. Both are fixed and covered by tests.

### Phase 4 — benchmark (done)
`hns_bench` loads a base prefix, queries and exact ground truth, builds HNSW
once, sweeps efSearch with warmup and repeated timed passes, measures exact
BruteForce as the baseline, and writes a structured JSON report with dataset,
config, build time, RSS, recall, QPS, latency percentiles, distance counts,
compiler and hardware.

SIFT-100k ground truth (10k queries, top 100) was generated with `hns_gt` in
55.4 s. Release run: build 36.1 s, peak RSS 92.5 MB. At `efSearch=50`,
**R@1 0.9926, R@10 0.9839, 6,895 QPS**, p95 0.184 ms, versus exact search at
193.5 QPS — a 35.6x speedup at that recall. The exact baseline matched the
ground-truth distances. `docs/results/bench.json` holds the full curve.

52 tests pass in release; the full suite is ASan/UBSan clean.

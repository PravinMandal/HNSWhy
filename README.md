# HNSWhy

A from-scratch C++20 HNSW vector index, measured against exact search on SIFT.

> **Status: in development.** Sessions 1–2 are complete: build skeleton, squared-L2
> distance kernel, tests, and a synthetic timing smoke test. No benchmark numbers
> have been measured yet. The results table below stays `TBD` until the S22 release
> run; every published number will come from a frozen, reproducible artifact with a
> commit and a command.

---

## What it is

HNSW is a layered proximity graph for approximate nearest-neighbour search. This
project implements it from scratch — no FAISS, no hnswlib, no usearch — and measures
the recall–QPS tradeoff against brute force.

The point is not "an HNSW implementation." The point is **a measurement you can
audit**: exact ground truth, a recall–QPS curve, latency percentiles, build time,
memory, and a replay of the graph walk that produced a query's answer.

**v1 target:** 100,000 SIFT vectors, 10,000 queries, single-threaded, in-memory,
squared L2. SIFT-1M is an optional one-configuration scale run after v1.

Why 100k? It is 51.2 MB of vectors, builds in minutes, and uses a ground truth
generated over the exact prefix. SIFT-1M is 512 MB and makes every sweep and every
brute-force pass 10× more expensive. A valid 100k row beats an invalid 1M row.

---

## Results

| Dataset | N | Queries | R@10 | QPS | p95 | Build | RSS | Exact QPS | Speedup |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT-100k | 100,000 | 10,000 | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SIFT-1M (optional) | 1,000,000 | 10,000 | TBD | TBD | TBD | TBD | TBD | TBD | TBD |

No number goes in this table without:

- a `run_id` and a CSV row;
- a commit SHA with `dirty=false`;
- dataset / query-set / ground-truth SHA-256;
- CPU, RAM, compiler, flags, thread count;
- a 95% interval for recall and a trial range for QPS/p95.

---

## Architecture

```text
data/*.fvecs ──► hns_io ──► VectorStore (flat) ──► BruteForce / HNSW
                                                      │
                                                      ▼
                                              recall + latency
                                                      │
                                              hns_bench ──► CSV + JSON
                                                              │
                                                              ▼
                                                    web/ (static site)
```

- `hns_core` — algorithms; no `main`, no file I/O, no JSON.
- `hns_io` — fvecs/ivecs, CSV, JSON, manifest, checksums.
- `hns_bench` — CLI, runner, RSS, export mode; the only `main()`.
- `hns_tests` — unit/invariant tests and the recall gate.
- `web/` — static React/TypeScript showcase; reads exported JSON.
- `docs/` — planning, architecture, workflow, and showcase specs.

Design decisions and the full component map are in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
[`docs/DECISIONS.md`](docs/DECISIONS.md).

---

## Build and test (Session 1)

Requires CMake 3.25+, Ninja, a C++20 compiler, and GTest.

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release

# build type, compiler, and flags
./build/release/hns_bench --version

# synthetic timing smoke test (not a benchmark result)
./build/release/hns_bench --synthetic-bench
```

## Benchmark (example; the runner arrives at S18)

```bash
./build/release/hns_bench \
  --dataset sift100k \
  --base data/sift/sift_base.fvecs --limit 100000 \
  --query data/sift/sift_query.fvecs \
  --gt tests/fixtures/sift100k/gt.ivecs \
  --gt-dist tests/fixtures/sift100k/gt_dist.fvecs \
  --M 16 --efConstruction 200 --efSearch 50 --k 10 \
  --trials 5 --out results.csv
```

The full datasets are not committed (they are large). `scripts/fetch-data.sh` fetches
them; `tests/fixtures/sift10k/` is a self-contained CI bundle (base + 1k queries +
GT) so a clean clone can run the test suite. The script is added at S3.

---

## Documents

| File | What it is |
|---|---|
| [`docs/START-HERE.md`](docs/START-HERE.md) | the on-ramp: scope, dataset ladder, first session |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | the locked choices and why |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | components, ownership, memory, measurement |
| [`docs/WORKFLOW.md`](docs/WORKFLOW.md) | the 28-session plan and checkpoints |
| [`docs/HNSW-SPEC.md`](docs/HNSW-SPEC.md) | the algorithm contract and prune worked example |
| [`docs/SHOWCASE.md`](docs/SHOWCASE.md) | the static site and data contract |
| [`docs/plan.md`](docs/plan.md) | the deep algorithm/measurement spec |
| [`docs/SESSIONS.md`](docs/SESSIONS.md) | the session log |

---

## What is deliberately not here

- A live 1M backend, auth, or a database. The claim is measured locally; a cloud CPU
  would produce different numbers and a second source of truth.
- A wrapper around an ANN library. The graph is written from scratch.
- A RAG/chat app. This is an index and a measurement.
- A 1M sweep. 1M is one optional configuration after 100k is solid.
- A five-page dashboard. The showcase is three pages: claim, trace, prune.

---

## Limitations (v1)

- 100k graded; 1M only if the optional run is measured.
- Single-threaded; no concurrent writes or searches.
- In-memory only; no persistence.
- Squared L2 only; no cosine/filtering/range search.
- No physical deletion; mark-deleted is a stretch item.
- Numbers are local to the machine in `manifest.json`; they are not cloud numbers.
- HNSW is approximate: recall is reported with a CI, not assumed.

---

## What I'd do next

IVF-PQ for memory-bound scale; a filtered-search path with in-filtering during beam
expansion; a same-host baseline against hnswlib/FAISS at matched recall; and a
persistence format with a corruption test. These are stretch items, not v1 claims.

---

## License

TBD.

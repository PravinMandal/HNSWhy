# VectorForge / HNSWhy — Plan (v3)

> **This is the deep reference, not the build order.** The canonical scope is
> `DECISIONS.md`; the component map is `ARCHITECTURE.md`; the algorithm contract is
> `HNSW-SPEC.md`. If this file conflicts with `DECISIONS.md`, `DECISIONS.md` wins.
>
> **Status: planning and merge.** No engine code yet. The frontend source exists in
> the separate VectorForge project and will be imported and refactored as `frontend/`.
>
> **Last updated:** 2026-11-09.

---

## 0. Mission

Build a **RAG app a stranger can use**, backed by a **from-scratch C++20 HNSW engine**,
measured against exact search on SIFT-100k, with tests and a reproducible benchmark.

The name still means **HNSW + why**. If you cannot say why `M=16`, why
`efConstruction=200`, why the graph is directed, or why the product rebuilds its index
from SQLite, using your own numbers and your own words, it is not yours yet.

The one-sentence version, with placeholders until the release run:

> "VectorForge answers questions over uploaded documents using a from-scratch HNSW
> index. On 100,000 SIFT vectors, the same engine found **R%** of the true top-10 at
> **Q** queries per second, while exact search did **B** QPS — a measured **S×**
> speedup at that recall, with a reproducible command."

No number goes in that sentence until it comes from a frozen `bench.json`.

---

## 1. v1 definition

### 1.1 What ships

1. **Product:** React UI with Documents, Ask, Map, and Benchmark tabs. Upload/paste,
   list, delete, ask, cited answers, 2D map, benchmark report.
2. **Engine:** `vf_engine` static library — flat `VectorStore`, squared L2, brute
   force, HNSW insert/search/prune, directed adjacency, mark-deleted, `validate()`.
3. **Persistence:** SQLite is the source of truth for documents, chunks, and
   embeddings; the graph is rebuilt on startup.
4. **Benchmark:** `hns_bench` reads SIFT, builds once, runs the `efSearch` grid with
   three timed passes per point over 10,000 queries, and writes `bench.json`.
5. **Tests:** unit/invariant tests, two-cluster prune test, mark-delete test,
   persistence/restart test, API test with a fake Ollama, benchmark schema test.
6. **Docs:** README with real numbers and limitations; architecture, decisions,
   spec, plan, and product/showcase docs that match the code.
7. **Run:** documented local setup; clear Ollama requirements; no auth in v1.

### 1.2 What does not ship in v1

The cut list is `DECISIONS.md` D20. In short:

- no 1M sweep (one optional run after v1);
- no KD-tree, 3D map, multi-metric benchmark, or cosine-specific index;
- no physical graph deletion or compaction (SQLite rows are deleted; the in-memory
  node is mark-deleted);
- no graph snapshot/load;
- no concurrent engine access;
- no auth, multi-user, or tenancy;
- no PDF extraction (plain text and Markdown only);
- no agents, Graph RAG, or tool calling;
- no distributed search;
- no static showcase site, trace-replay page, or live benchmark endpoint.

Each returns only with a new decision entry and a reason tied to a user or a
measurement.

### 1.3 The correctness gates

- `siftsmall` BF vs shipped GT: tie-aware R@10 = 1.0000 (loader proof).
- `sift10k` / `sift100k` BF vs our own GT: tie-aware R@10 = 1.0000 (harness
  consistency check; the independent checks are `siftsmall` and the float64 sample).
- HNSW on `sift100k`: R@10 ≥ 0.90, 10,000 queries, at the SLO point.
- R@1 ≥ 0.85 is a diagnostic, not a second gate.
- The two-cluster test: nearest-only fails; heuristic passes.
- `validate()` green after every insert on small random graphs and after the 10k
  build.
- Mark-deleted: deleted IDs never returned; live neighbours still found; restart
  after delete works.
- Product: upload -> ask -> citations -> delete -> restart is green with a fake
  Ollama in CI.

If the loader/harness gate fails, stop. Do not tune the graph.

---

## 2. Dataset ladder and ground truth

### 2.1 Modes

| Mode | Base | Queries | Ground truth | Use |
|---|---:|---:|---|---|
| `siftsmall` | 10,000 | 100 | shipped `siftsmall_groundtruth.ivecs` | loader proof, smoke |
| `sift10k` | first 10,000 of 1M | 1,000 | **our BF top-100, IDs + distances** | CI, integration, fast tuning |
| `sift100k` | first 100,000 of 1M | 10,000 | **our BF top-100, IDs + distances** | graded v1 |
| `sift1m` | 1,000,000 | 10,000 | shipped `sift_groundtruth.ivecs` + BF sample | optional scale run |

`data/` is gitignored. The **self-contained CI bundle** is
`tests/fixtures/sift10k/`: first 10k base (5.1 MB), fixed 1k queries (0.5 MB), GT
IDs + distances (0.8 MB), query IDs, and SHA-256 (~6.5 MB total). The
`tests/fixtures/sift100k/` fixture contains GT IDs + distances + query IDs + hashes
(~8 MB), not the 100k base. A clean clone can run CI from the 10k bundle; a full
100k benchmark needs the download script and the local data.

### 2.2 Why the shipped key is only valid on 1M

The SIFT answer key is the top-100 over **all 1,000,000 base vectors**. For a prefix
of 100,000, the true nearest neighbours may live outside the prefix, so the shipped
IDs are not the top-100 of that subset. A perfect brute force on the prefix scores
far below 1.0 against the full-1M key. This is not a loader bug. Therefore:

- `siftsmall` uses its own shipped key (10k base / 100 queries).
- `sift10k` and `sift100k` use **our** BF top-100 over the exact prefix.
- `sift1m` uses the shipped key, with a BF cross-check on a deterministic sample.
- Never score a subset against the full-1M key.

### 2.3 Generating the subset GT

Use the same brute-force scan loop that is under test:

1. Load the exact base prefix (first 100k) and the exact query set (all 10k, or a
   pinned subset with its ID list).
2. For each query, compute the top-100 by `(distance, id)`.
3. Store IDs **and distances**. IDs alone cannot resolve ties or validate distance
   thresholds.
4. Write `gt.ivecs`, `gt_dist.fvecs`, `query_ids.txt`, and a SHA-256 file.
5. Record the generator commit, flags, `N`, `dim`, metric, and seed.
6. Validate a 100-query sample against an independent float64 reference. The sample
   must agree exactly after the tie rule.
7. Commit the fixture; do not regenerate it silently in CI.

### 2.4 Tie-aware recall

For query `q`:

```
G_q = { id : d(q, id) <= d_k(q) + eps }      // all true items at the k-th distance
R_q = returned top-k IDs
recall_q = |R_q ∩ G_q| / k
```

`eps = 1e-6` is a safety margin; SIFT squared-L2 is integer-valued in float32, so
exact equality is usually enough. Report ID-overlap recall separately:

```
id_overlap_q = |R_q ∩ GT_ids_q| / k
```

The headline uses distance-threshold recall. ID overlap is diagnostic.

### 2.5 Query counts, trials, and uncertainty

- Headline / README / benchmark tab / resume / SLO: **10,000 queries**.
- CI smoke: **1,000 fixed queries**, labelled `PRELIMINARY`.
- Timed passes: **three** over the same build, after a 100-query warmup.
- Report the mean and the min/max across passes. A 95% bootstrap interval is
  optional in `bench.json`; it is not a gate.
- `siftsmall`'s 100 queries: loader proof only; no statistical claim.

The old plan gated a 1k subset on a 0.90 lower bound. At `p ≈ 0.90`, the 95% half-width
at n=1,000 is about ±1.9 pp, so a good 0.92 index would pass only about half the time.
That is a coin flip, not a quality gate. The CI smoke test therefore uses a fixed
1,000-query set and a simple `R@10 ≥ 0.90` point floor, labelled preliminary. The
headline is always 10,000 queries.

If a confidence interval is computed, use a query-level bootstrap for mean recall.
Wilson is for binary metrics only (exact R@1, perfect-top-10 rate). Do not apply
Wilson to a mean of per-query fractions as if each query were one Bernoulli trial.

---

## 3. Data formats and loader

### 3.1 fvecs

Each record:

```
int32 dim (little-endian)
float data[dim] (little-endian)
```

For SIFT: `dim=128`, record size = 4 + 128×4 = 516 bytes.

### 3.2 ivecs

Same layout with `int32 data[dim]`. Ground-truth records are usually 100 ints.

### 3.3 Loader rules

1. Open in binary; read the first 4 bytes; validate `dim` against the expected value
   and a sane upper bound (e.g. 1..4096).
2. Compute `record_size = 4 + dim*4`.
3. Get the file size; compute `n = file_size / record_size`; reject a remainder.
4. `reserve(n)` the flat store; read each record directly into it. Do not read the
   whole file into a `vector<char>` and then copy.
5. Check `gcount` per record; reject truncated files.
6. Reject wrong dim, empty files, and absurd counts with a clear error.
7. Compute SHA-256 of the file for `bench.json`.
8. For ivecs, validate the ID range (`0 ≤ id < N`) and the record length.
9. Reject `--n` greater than the base count and `--k` greater than the ground-truth
   depth. The v1 GT depth is 100.

No `reinterpret_cast` on the record header; use `memcpy` into an `int32_t` and
convert. Document little-endian; SIFT is little-endian. A big-endian reader is a
stretch item.

### 3.4 Product vectors

The product does not use fvecs. Embeddings are stored in SQLite as float32 BLOBs with
a dimension column. The loader checks:

- `dim` matches the configured embedding model;
- `blob.size() == dim * sizeof(float)`;
- values are finite;
- the vector is normalized at the product boundary (the engine does not normalize).

### 3.5 External IDs

v1 uses slot IDs (`uint32_t`). SIFT does not need external IDs. The product maps slots
to chunk IDs outside the engine. If a future dataset needs external IDs, add a
separate `external_id` array and an optional lookup map; do not put a map in the hot
path and do not widen the slot type without a boundary test.

---

## 4. Engine architecture and ownership

### 4.1 One owner

`VectorStore` owns the flat `std::vector<float>`. `BruteForce` is a view over it.
`HNSW` holds `const VectorStore&` and owns only graph state. No index copies the
vectors. At 1M, a duplicate store is 512 MB and makes the BF cross-check ambiguous.

### 4.2 Minimal interface

```text
SearchableIndex
  search(span<const float> query, size_t k, size_t ef) -> SearchResult
  size() -> size_t
  dim() -> size_t
```

`SearchResult` = top-k IDs/distances + `SearchStats`. Mutation is not in the base
interface. `HNSW` has `build()`, `insert(slot)`, and `remove(slot)` for
construction/tests/product deletion. BruteForce does not implement mutation.

### 4.3 Layer 0 adjacency

Fixed-capacity flat adjacency, not CSR:

```text
links0_:  N * M0 uint32_t
degree0_: N uint16_t
```

`M0 = 2*M` (32 for M=16). Insert into a free slot; prune by swap-erase. Upper layers
are sparse and may use per-node small vectors. Grow the flat arrays geometrically when
`N` exceeds capacity; slot IDs and existing links stay valid. A rebuild per upload is
not acceptable. The full invariant list is in `HNSW-SPEC.md` §3.

### 4.4 Basic guarantee, not strong

Validate inputs first; reserve what is cheap. If `bad_alloc` occurs, mark the index
dirty and have the server rebuild on the next start. Do not promise rollback of graph
mutations in v1. The throwing-allocator test is a stretch item.

### 4.5 Single-threaded

One engine thread. No `shared_mutex`, no TSan, no contention benchmark. The README
says "1 engine thread; API serializes access." A thread-safe wrapper is a stretch item,
added only if a real consumer exists.

### 4.6 Directed graph

The graph is directed. Edges are added in both directions initially; pruning is
one-sided. There is no symmetry invariant. The reasons and the validation rules are in
`HNSW-SPEC.md` §3 and `DECISIONS.md` D10.

### 4.7 Mark-deleted

`HNSW::remove(slot)` marks the node deleted. The node stays as a router, search filters
it from results, and `size()` counts only live vectors. The product deletes the SQLite
document row; on restart the graph is rebuilt from the remaining rows. See
`HNSW-SPEC.md` §8.

---

## 5. HNSW requirements

The full contract is `HNSW-SPEC.md`. The non-negotiables:

1. **Directed graph** in v1; no reciprocal cleanup, no symmetry invariant.
2. **Degree caps**: `M0` at layer 0, `M` above.
3. **Level RNG**: seeded, `u ∈ (0,1]`, `mL = 1/ln(M)`, `maxLevel` clamp, `M ≥ 4`.
4. **Insert order**: connect at `min(level, oldTop)` down to 0, then promote
   `entry`/`topLayer`. `levelCount[l]` counts nodes with `level >= l`.
5. **Back-edge repair**: prune `u`'s list using distances to `u`, not to the new node.
6. **Search**: greedy descent with `ef=1` above layer 0; beam with
   `ef_eff = max(efSearch, k)` at layer 0; terminate when the closest candidate is
   worse than the worst result and the result set is full.
7. **Per-layer visited**: do not share one global tag across layers.
8. **Comparator**: `(distance, id)` everywhere; deterministic ties.
9. **Prune heuristic**: keep candidate `c` iff for every selected `s`,
   `d(c,s) > d(c,ref)`. Worked example in `HNSW-SPEC.md` §7.2.
10. **`validate()`**: no self/duplicate edges, degree caps, layer membership, entry at
    `topLayer`, `levelCount` consistency, no symmetry check.
11. **Mark-deleted**: query traverses tombstones and filters them from results;
    insertion treats them as candidates so a delete-all does not isolate the next
    insert. Count live vs slot_count.

No physical delete. No graph snapshot. No concurrent mutation. Those are stretch
items, not v1.

---

## 6. Product integration

### 6.1 SQLite is the source of truth

Schema (same as `ARCHITECTURE.md` §4):

```sql
documents(id, title, text, created_at)
chunks(id, document_id, text, created_at)
embeddings(chunk_id, dim, vector BLOB)
```

The engine never sees SQL. Engine slots are runtime-only; the product keeps an
in-memory `slot_to_chunk_` map. The API exposes chunk IDs, never slots.

### 6.2 Startup

```text
open SQLite
  -> load chunks ordered by id
  -> load embeddings joined on chunk_id
  -> assign slots 0..n-1 in that order
  -> validate dim/length
  -> VectorStore::reserve(n, dim); add each
  -> HNSW::build()
  -> start HTTP server
```

The rebuild is measured for the product dataset, not mixed into the SIFT benchmark.

### 6.3 Insert

```text
validate text
  -> chunk
  -> embed each chunk
  -> normalize
  -> SQLite transaction: document + chunks + embeddings (keyed by chunk_id)
  -> commit
  -> slot = VectorStore::add(vector); HNSW::insert(slot); update slot_to_chunk_
  -> update live counts
```

SQLite commits first. If the process dies before the graph inserts, the next startup
rebuild repairs the graph. If an in-memory insert fails after commit, return an error
and rebuild; the database remains the source of truth.

### 6.4 Delete

```text
delete the document row (cascade chunks and embeddings)
  -> HNSW::remove(slot) using the runtime map
  -> update live counts
```

On restart, the deleted document is gone from SQLite; the rebuilt graph has no
tombstones. No physical graph compaction in v1.

### 6.5 Ask

```text
embed(question) -> normalize
  -> HNSW::search(q, k=5, ef=50)
  -> map slots to live chunks
  -> if no live chunks: grounded "not found" (no LLM call)
  -> prompt with numbered sources
  -> Ollama generate
  -> answer + citations + timings

There is no hard distance cutoff in v1; a threshold is embedding-model dependent and
uncalibrated. If the index has chunks, retrieve top-k and let the constrained prompt
decide whether the sources answer the question. A refusal with sources is a valid
grounded result.
```

The answer is grounded in the retrieved chunks. If Ollama is down, return `503` with a
fixable message. Do not answer from general knowledge and present it as document
evidence.

### 6.6 Map

Backend samples up to 2,000 live chunks (default 1,000) and returns their vectors plus
metadata. Frontend computes 2D PCA and draws the points. The map is labelled as a 2D
projection; it is not a benchmark.

### 6.7 API and auth

Endpoints and error shape are in `ARCHITECTURE.md` §7. No auth in v1. The demo is for a
local or trusted environment. A public deployment needs an access gate or an explicit
"anyone can upload/delete" warning.

---

## 7. Measurement protocol

### 7.1 Configuration and build

A configuration is `(dataset, M, efConstruction)`. Build once; measure build time
once; warm up 100 queries; run **three** timed passes against the same build.
`efSearch` sweeps reuse the build. A fresh process is used per configuration, not per
trial. The release report uses the default grid `10,20,50,100,200`; `--ef` is the
headline point, and every grid point gets the same three-pass treatment. The CLI
rejects an `--ef` that is not in `--ef-grid`.

No graph persistence in v1. If per-trial process isolation is ever required, it waits
for a graph snapshot (stretch).

The BF baseline is computed once per `(dataset, query set, k)` and cached. It uses the
same distance kernel, compiler flags, thread count, and query batch as HNSW. Do not
rebuild it for every HNSW configuration.

### 7.2 Metrics

| Metric | Definition |
|---|---|
| `R@1/10` | mean per-query fraction, tie-aware; R@1 is the top of the same top-10 result |
| `id_overlap` | mean per-query ID-overlap fraction |
| `perfect_top10` | fraction of queries with tie-aware R@10 = 1.0 |
| `QPS` | `n_queries / wall_s` per trial; mean and min/max across trials |
| `p50/p95/p99` | per-trial percentile of per-query latency; mean and range |
| `build_s` | HNSW build only, excluding fvecs/GT load; one build, timed separately |
| `inserts_per_s` | `N / build_s` |
| `build_dist_comps` | distance calls during build |
| `rss_peak` / `rss_steady` | `VmHWM` / post-warmup RSS |
| `bytes_per_vector` | total index bytes / N |
| `avg_distance_computations` | mean per query; define counting rule |
| `avg_visited` | mean per query |
| `bf_qps` | exact scan, same query set/kernel/flags/thread |
| `speedup` | `qps / bf_qps` at the SLO point |
| `p95_ratio` | `bf_p95 / hnsw_p95` |
| `dist_ratio` | `N / avg_distance_computations` |

Counting rule: increment `distance_computations` once per distance evaluation between
the query and a stored vector. Visited nodes and distance computations are not the same
number; report both.

### 7.3 Latency methodology

- Per-query samples with `std::chrono::steady_clock`.
- Consume the top-k (checksum) so the optimizer cannot delete the scan.
- Warm up 100 queries; run three timed passes over the query set.
- Compute p50/p95/p99 per trial; report the mean across trials and the min/max range.
  Do not mix QPS from one trial with p95 from another.
- Pin to one physical P-core (`taskset -c <core>`); record CPU model, SMT sibling,
  core count, RAM, kernel, governor/turbo. Close background load.
- Headline is single-threaded. Multi-thread is a stretch item.
- `-O3 -DNDEBUG`; `-march=native` only for the headline, recorded. Tests use
  ASan/UBSan in a separate build.

### 7.4 SLO rule

Pre-register: **headline = the lowest-latency configuration whose R@10 on `sift10k` is
≥ 0.90.** Report the actual `R@10` and QPS on `sift100k` for that configuration. Plot
the full Pareto frontier from the report's `curve`; show dominated points; no
interpolation. The headline point is marked even if another 100k curve point looks
better, because selection happened on `sift10k` and is not re-run on the report set.
R@1 is diagnostic.

Do not select the fastest of many grid points on the same queries and then present its
number as if it were unbiased. For v1, the simplest defensible rule is: tune on
`sift10k`, report on `sift100k` with all 10k queries. If the 100k result is below the
gate, report that and fix the config; do not silently move the gate.

### 7.5 Speedup claim

Report three ratios separately:

- QPS ratio at the SLO point;
- p95 ratio;
- distance-computation ratio (`N / avg_distance_computations`).

Explain why wall-clock speedup is smaller than the distance ratio: brute force streams
memory sequentially, while HNSW random-accesses the graph. Use the same BF kernel and
flags. Do not hardcode "500×"; at 100k, the measured ratio will be smaller than the old
1M example. If a 1M row is measured, it gets its own labeled claim.

### 7.6 Product latency is not the benchmark

`/api/ask` includes embedding, HNSW search, SQLite lookup, prompt construction, and
Ollama generation. That is a product latency, not a retrieval benchmark. Report it in
the UI if useful, but never put it in the SIFT table.

### 7.7 Ablation is secondary

The optional ablation compares `--prune nearest` with `--prune heuristic` at the
headline configuration, same build budget. Publish both rows or omit the ablation;
never imply one from the other. It is not a v1 UI panel. The main headline is the
product + the HNSW row, not the ablation.

---

## 8. Output schema

`hns_bench` writes one `bench.json`. The release artifact is committed as
`docs/results/bench.json`; the raw dataset stays in gitignored `data/`. The UI reads it
through `/api/benchmark`. The schema is intentionally smaller than the old
CSV/manifest ceremony:

```json
{
  "schema": 1,
  "run_id": "2026-11-09T12-00-00Z-abcdef0",
  "created_at": "2026-11-09T12:00:00Z",
  "git": { "commit": "0123456789abcdef0123456789abcdef01234567", "dirty": false },
  "machine": {
    "cpu": "AMD Ryzen ...",
    "cores": 8,
    "ram_gb": 16,
    "kernel": "Linux ..."
  },
  "build": {
    "type": "Release",
    "compiler": "g++ 14.2",
    "flags": "-O3 -DNDEBUG -march=native",
    "threads": 1,
    "pinning": "taskset -c 2"
  },
  "dataset": {
    "name": "sift100k",
    "n": 100000,
    "dim": 128,
    "metric": "l2",
    "sha256": "..."
  },
  "queries": { "n": 10000, "sha256": "..." },
  "groundtruth": {
    "source": "brute-force-prefix",
    "k": 100,
    "ids_sha256": "...",
    "dist_sha256": "..."
  },
  "params": {
    "M": 16,
    "M0": 32,
    "efConstruction": 200,
    "efSearch": 50,
    "k": 10,
    "seed": 42,
    "prune": "heuristic"
  },
  "curve": [
    { "efSearch": 10, "r10": 0, "qps": 0, "p95_ms": 0 },
    { "efSearch": 20, "r10": 0, "qps": 0, "p95_ms": 0 },
    { "efSearch": 50, "r10": 0, "qps": 0, "p95_ms": 0 },
    { "efSearch": 100, "r10": 0, "qps": 0, "p95_ms": 0 },
    { "efSearch": 200, "r10": 0, "qps": 0, "p95_ms": 0 }
  ],
  "trials": [
    {
      "qps": 0,
      "p50_ms": 0,
      "p95_ms": 0,
      "p99_ms": 0,
      "r1": 0,
      "r10": 0,
      "id_overlap": 0,
      "perfect_top10": 0,
      "avg_distance_computations": 0,
      "avg_visited": 0
    }
  ],
  "summary": {
    "r1": 0,
    "r10": 0,
    "r10_min": 0,
    "r10_max": 0,
    "id_overlap": 0,
    "perfect_top10": 0,
    "qps": 0,
    "qps_min": 0,
    "qps_max": 0,
    "p50_ms": 0,
    "p50_min_ms": 0,
    "p50_max_ms": 0,
    "p95_ms": 0,
    "p95_min_ms": 0,
    "p95_max_ms": 0,
    "p99_ms": 0,
    "p99_min_ms": 0,
    "p99_max_ms": 0,
    "build_s": 0,
    "inserts_per_s": 0,
    "build_dist_comps": 0,
    "rss_peak_mb": 0,
    "rss_steady_mb": 0,
    "bytes_per_vector": 0,
    "avg_distance_computations": 0,
    "avg_visited": 0
  },
  "baseline": {
    "bf_qps": 0,
    "bf_p95_ms": 0,
    "speedup": 0,
    "p95_ratio": 0,
    "dist_ratio": 0
  },
  "limitations": [
    "single-threaded",
    "local machine, not a cloud benchmark"
  ]
}
```

Rules:

- Every report carries commit, dirty flag, hashes, flags, thread count, and machine.
- `schema` is an integer; bump it on any change.
- `git.commit` is the full 40-character SHA, not an abbreviation.
- `summary` carries the mean and the min/max across timed passes; `trials` carries the
  raw per-pass rows. The README range is read from `summary`, not recomputed in the UI.
- `curve` is measured on the report dataset at each `--ef-grid` value; each point is
  the mean of the same three-pass protocol. `trials` holds the raw passes for the
  headline `params.efSearch` only. Do not pick the best curve point after the fact: the
  headline is the pre-registered SLO point.
- The validator refuses missing required fields and `dirty=true` for a published
  report.
- The README table and Benchmark tab read `summary`; they never recompute a headline.
- A per-query sidecar is optional and not required for v1. If it exists, it is an
  audit aid, not the product.

---

## 9. CI vs release

### 9.1 Per-PR

- Build `vf_engine`, `vectorforge_server`, `hns_bench`, `hns_tests`.
- Run unit/invariant tests.
- Run recall on `sift10k` with our own GT: 1,000 fixed queries, `R@10 ≥ 0.90`,
  labelled `PRELIMINARY`.
- Run the two-cluster prune test.
- Run the mark-delete and persistence/restart tests.
- Run the API test with a fake Ollama.
- Check deterministic counters, degree bounds, level histogram, and L0 reachability.
- No QPS/p95 gate on shared runners; those are noise.

### 9.2 Release

- `sift100k`, all 10k queries, one build, three timed passes.
- Build time, RSS, recall, curve diff.
- Block on a >0.5 pp recall drop or a >10% distance/RSS regression against the last
  frozen run.
- Publish only a pinned run on named hardware with `dirty=false`.

A 10k gate does not certify 100k. Say so explicitly in the README. The release run is
the headline.

---

## 10. Tests

### 10.1 Unit / property

- distance: hand-checked, symmetry of `d(a,b)=d(b,a)`, zero vector, NaN policy,
  float64 reference;
- fvecs/ivecs: truncated, wrong dim, empty, record-size remainder, ID range;
- store: add/get/size/dim, dim mismatch, no reallocation during read, self-distance 0;
- BF: exact vs `sort` on small random, ties by ID, k>N, empty, same as GT generator;
- recall: tie-aware vs ID overlap, hand examples, optional bootstrap CI.

### 10.2 HNSW

- `validate()` after every insert on N ≤ 200;
- level histogram ~1/M; cap; entry at top; `levelCount`;
- greedy descent on a hand-built line;
- beam matches exhaustive for N ≤ 20;
- insert 0/1/2; connection before promotion; back-edge repair; no self/duplicate;
- prune worked example; two-cluster kill test; fill-to-M flag;
- search edges: empty, k=1, k>N, ef=1, ef>N, duplicate distances, ties;
- mark-delete: deleted IDs never returned; live neighbours still found; entry safe;
- full 10k: report R@10 at the chosen config; do not add a second 0.95 floor;
  full 100k: R@10 ≥ 0.90 (release).

### 10.3 Product / IO / bench

- SQLite insert -> restart -> rebuild -> same search results;
- delete -> restart -> gone;
- API upload/ask/delete with a fake Ollama; error paths; no partial document;
- fvecs/ivecs parsing; `bench.json` schema; dirty-tree refusal;
- fake clock percentiles; query-ID hash; RSS parser.

The two-cluster test is permanent. Do not delete it once the heuristic works; it is
the guard against the exact bug the project is about.

---

## 11. Memory and performance

### 11.1 100k SIFT

| Thing | Size |
|---|---:|
| Base vectors | 51.2 MB |
| L0 links (M0=32) | 12.8 MB |
| L0 degree | 0.2 MB |
| Upper links/degree | ~0.5 MB |
| Levels/entry/counts | ~0.1 MB |
| Visited tags | 0.4 MB |
| **Engine total** | **~65 MB** |
| Queries (10k) | 5.1 MB |
| GT IDs + distances | 8.0 MB |
| **Benchmark process** | **~78 MB** |

### 11.2 Product chunks

| Dataset | Vectors | L0 links | Engine total |
|---|---:|---:|---:|
| 10k chunks, 768D | 30.7 MB | 1.3 MB | ~33 MB |
| 100k chunks, 768D | 307 MB | 12.8 MB | ~322 MB |

The SQLite file is roughly the vector bytes plus text. At 10k chunks, the process is
comfortable on a laptop. At 100k chunks, startup rebuild and disk become noticeable;
that is the upper end of the v1 target, not production scale.

### 11.3 1M (optional)

| Thing | Size |
|---|---:|
| Base vectors | 512 MB |
| L0 links | 128 MB |
| L0 degree | 2 MB |
| Upper links/degree | ~4.4 MB |
| Levels/counts | ~1 MB |
| Visited tags | 4 MB |
| **Engine total** | **~651 MB** |
| Queries + GT | 13 MB |
| **Benchmark process** | **~664 MB** |

No duplicate store. Peak under ~800 MB with allocator overhead. The first blowup at
10× is duplicate stores, then per-node `(maxLevel+1)*M0` blocks, then the loader's
temporary buffer. Measure `VmHWM`/`ru_maxrss`; do not guess in the README.

### 11.4 Expected performance (estimates, not claims)

At 100k, brute force scans 100k vectors per query. HNSW query time grows sublinearly
in practice. The measured speedup at 100k will be smaller than the old 1M example.
Report the measured ratio; do not promise a number. Build time is dominated by
`N * efConstruction * M` distance work; measure it.

---

## 12. Build and tooling

- C++20, CMake ≥ 3.20, Ninja, g++ or clang++.
- `vf_engine` (no SQL/HTTP/Ollama), `vectorforge_server`, `hns_bench`, `hns_tests`.
- SQLite3 system library; `cpp-httplib` and `nlohmann/json` header-only.
- Ollama running locally for the product; not required for the benchmark or tests.
- Frontend: existing React/Vite; `npm ci && npm run build`.
- Presets: `dev` (ASan/UBSan), `release` (`-O3 -DNDEBUG`), `release-native`
  (`-march=native`), `ci`.
- Print build type/compiler/flags at startup; put them in every `bench.json`.
- No raw `new/delete` in the engine; no `using namespace` in headers; headers
  self-contained.
- `-Wall -Wextra` from the first build; `-Werror -Wconversion` later, after the core
  is stable.
- No `aligned_alloc` claim while using `std::vector<float>`; add a custom allocator
  only if measured.

`main.cpp` is bootstrap only: open SQLite, load, build, register routes, listen. If it
starts to contain HNSW or SQL logic, move that logic to `engine/` or `product/`.

---

## 13. Stretch (ordered)

1. **1M one-config scale run** — after v1 ships; one build, 10k queries for recall,
   separate row; never a sweep.
2. **Graph snapshot + load** — after the format is frozen; versioned, checksummed,
   corrupt-one-byte test; never the source of truth.
3. **Physical compaction** — after mark-deleted is stable; connectivity tests.
4. **Concurrent read path** — coarse `shared_mutex` + TSan; only with a real consumer.
5. **PDF extraction** — a text extraction dependency, with a failure path.
6. **Auth / access gate** — before any public multi-user deployment.
7. **Reranker or hybrid search** — measured on the product set, not folded into the
   SIFT claim.
8. **Streaming answer** — product polish, not a retrieval change.
9. **Docker packaging and a public deployment** — one VM or container; Ollama hosted
   separately or on the same machine.
10. **Same-host baseline** against hnswlib/FAISS at matched recall.
11. **Quantization** for memory-bound scale.

Each gets its own decision entry and its own measurement. None silently changes the
v1 claim.

---

## 14. Definition of done

- [ ] Product: upload/paste, list, delete, ask, citations, 2D map, benchmark tab.
- [ ] Persistence: restart preserves documents and search results.
- [ ] Engine: BF, HNSW insert/search/prune, directed graph, mark-deleted, `validate()`.
- [ ] Tests green, including two-cluster, mark-delete, persistence, and API tests.
- [ ] `sift100k` HNSW R@10 ≥ 0.90, 10k queries, three timed passes.
- [ ] `bench.json` with commit, hashes, flags, machine, trials, summary.
- [ ] README: short documented setup, real numbers, no placeholders, limitations, CPU/RAM.
- [ ] Benchmark tab: real report, parameters, command; no invented chart.
- [ ] No fake embeddings, no hardcoded speedup, no "production-grade" claim.
- [ ] 20-minute whiteboard and 60-second demo, no notes.
- [ ] `DECISIONS.md` matches the implementation; no silent scope creep.
- [ ] Final tag; optional 1M row clearly separated.

If only the engine works, the project is a measured index without a product. If only
the product works, it is a RAG wrapper without depth. v1 is the pair.

---

## 15. Open questions

1. **Embedding model default:** `nomic-embed-text` (768-d) or a smaller model for
   speed. Pick one, record the dimension, and make it configurable.
2. **Chunk size:** start at ~350 words with ~50-word overlap; measure retrieval on a
   small labeled set before tuning.
3. **LLM default:** a local Ollama model that fits the demo machine; make it an env
   var, not a hardcoded prompt dependency.
4. **PDF extraction:** after v1. Plain text and Markdown prove the flow.
5. **Public deployment:** a local documented run is v1; a public demo needs an access
   gate and a host that can run Ollama.
6. **Graph snapshot:** only after the product rebuild is measured and stable.
7. **Product size target:** 10k chunks is comfortable; 100k is the upper end. Revisit
   after measuring startup and search latency.
8. **Map sample size:** default 1,000, cap 2,000. Revisit if the payload is slow.

When an open question is resolved, add a `DECISIONS.md` entry and link it here.

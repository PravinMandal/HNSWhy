# HNSWhy — Algorithm & Measurement Specification (v2)

> **This is the deep reference, not the schedule.** The canonical scope is
> `DECISIONS.md`; the component map is `ARCHITECTURE.md`; the algorithm contract is
> `HNSW-SPEC.md`; the 28-session plan is `WORKFLOW.md`. If this file conflicts with
> `DECISIONS.md`, `DECISIONS.md` wins.
>
> Status: Session 1 complete; Session 2 is next.
> Last updated: 2026-10-22.

---

## 0. Mission

Build a **C++20 HNSW vector index from scratch**, measured against exact search on
SIFT, with a reproducible recall–QPS curve and an interview-defensible explanation of
every parameter.

The name is the thesis: **HNSW + why**. If you cannot say why `M=16`, why
`efConstruction=200`, or why the pruning heuristic exists, using your own numbers, it
is not yours yet.

---

## 1. v1 definition

### 1.1 What ships

1. **Core library** (`hns_core`): flat `VectorStore`, squared L2, brute force, HNSW
   insert/search/prune, multi-layer descent, deterministic RNG, tie-aware recall,
   `validate()`.
2. **Benchmark** (`hns_bench` + `hns_io`): fvecs/ivecs readers, build-once/query-many
   runner, p50/p95/p99, QPS, build time, RSS, distance computations, CSV/JSON.
3. **Curve**: at least 6 measured points across `M`, `efConstruction`, `efSearch`,
   full Pareto, CIs, pre-registered SLO row.
4. **Tests + CI**: unit/invariant tests, two-cluster kill test, trace equality, recall
   gate on `sift10k` with our own GT.
5. **Static showcase** (planned v1; cut only if a hard deadline forces it): `/`,
   `/trace`, `/prune`; three pages.
6. **README**: one command, real numbers, limitations, no placeholders.

### 1.2 What does not ship in v1

No physical delete, persistence, concurrency, server, WASM, cosine dataset, pgvector,
KDTree, filtering, range search, quantization, or 1M sweep. These are stretch items
in §12. Each gets its own decision entry before it enters the schedule.

### 1.3 The correctness gate

- `siftsmall` BF vs shipped GT: tie-aware R@10 = 1.0000 (loader proof).
- `sift10k` / `sift100k` BF vs our own GT: tie-aware R@10 = 1.0000 (harness proof).
- HNSW on `sift100k`: R@10 lower bound ≥ 0.90, 10,000 queries, at the SLO point.
- R@1 ≥ 0.85 is a diagnostic, not a second gate.
- The two-cluster test: nearest-only fails; heuristic passes.
- `validate()` green after every insert on small random graphs and after the 10k build.
- `searchTraced` == `search` for 100 queries.

If the loader/harness gate fails, stop. Do not tune the graph.

---

## 2. Dataset ladder and ground truth

### 2.1 Modes

| Mode | Base | Queries | Ground truth | Use |
|---|---:|---:|---|---|
| `siftsmall` | 10,000 | 100 | shipped `siftsmall_groundtruth.ivecs` | loader proof, smoke |
| `sift10k` | first 10,000 of 1M | 1,000 or 10,000 | **our BF top-100, IDs + distances** | CI, integration, fast tuning |
| `sift100k` | first 100,000 of 1M | 10,000 | **our BF top-100, IDs + distances** | graded v1 |
| `sift1m` | 1,000,000 | 10,000 | shipped `sift_groundtruth.ivecs` + BF sample | optional scale run |

`data/` is gitignored. The **self-contained CI bundle** is
`tests/fixtures/sift10k/`: first 10k base (5.1 MB), fixed 1k queries (0.5 MB), GT
IDs + distances (0.8 MB), query IDs, and sha256 (~6.5 MB total). The
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
4. Write `gt.ivecs`, `gt_dist.fvecs`, `query_ids.txt`, and a `sha256` file.
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

### 2.5 Query counts and uncertainty

- Headline / README / showcase / resume / SLO: **10,000 queries**.
- PR smoke / regression: **1,000 fixed queries**, labelled `PRELIMINARY`, paired
  against a frozen baseline.
- `siftsmall`'s 100 queries: loader proof only; no statistical claim.

At `p ≈ 0.90`:

| n | Approx. 95% half-width | Observed needed for lower bound ≥ 0.90 | Power at true 0.92 |
|---:|---:|---:|---:|
| 1,000 | ±1.9 pp | ≈0.919 | ≈56% |
| 10,000 | ±0.6 pp | ≈0.906 | >99% |

This table is a normal approximation for planning, not the published CI. A 0.90
lower-bound gate on 1k queries is a coin flip for a good 0.92 index. Do not publish a
1k number as the headline.

For mean recall, use a **query-level bootstrap** (10,000 resamples) for the 95% CI.
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

Same layout with `int32 data[dim]`. Ground truth records are usually 100 ints.

### 3.3 Loader rules

1. Open in binary; read the first 4 bytes; validate `dim` against the expected value
   and a sane upper bound (e.g. 1..4096).
2. Compute `record_size = 4 + dim*4`.
3. Get the file size; compute `n = (file_size) / record_size`; reject a remainder.
4. `reserve(n)` the flat store; read each record directly into it. Do not read the
   whole file into a `vector<char>` and then copy.
5. Check `gcount` per record; reject truncated files.
6. Reject wrong dim, empty files, and absurd counts with a clear error.
7. Compute sha256 of the file for the manifest.
8. For ivecs, validate the ID range (0 ≤ id < N) and the record length.

No `reinterpret_cast` on the record header; use `memcpy` into an `int32_t` and
convert. Document little-endian; SIFT is little-endian. A big-endian reader is a
stretch item.

### 3.4 External IDs

v1 uses slot IDs (`uint32_t`). SIFT does not need external IDs. If a future dataset
does, add a separate `external_id` array and an optional lookup map; do not put a map
in the hot path and do not widen the slot type without a boundary test.

---

## 4. Index architecture and ownership

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
interface. `HNSW` has `build()` and `insert(slot)` for construction/tests. BruteForce
does not implement mutation.

### 4.3 Layer 0 adjacency

Fixed-capacity flat adjacency, not CSR:

```text
links0_:  N * M0 uint32_t
degree0_: N uint16_t
```

`M0 = 2*M` (32 for M=16). Insert into a free slot; prune by swap-erase. Upper layers
are sparse and may use per-node small vectors. The full invariant list is in
`HNSW-SPEC.md` §3.

### 4.4 Basic guarantee, not strong

Validate inputs first; reserve what is cheap. If `bad_alloc` occurs, mark the index
dirty and have the CLI exit with "rebuild required." Do not promise rollback of graph
mutations in v1. The throwing-allocator test is a stretch item.

### 4.5 Single-threaded

One thread. No `shared_mutex`, no TSan, no contention benchmark. The README says
"1 thread." A thread-safe wrapper is a stretch item, added only if a real consumer
exists.

---

## 5. HNSW requirements

The full contract is `HNSW-SPEC.md`. The non-negotiables:

1. **Symmetric graph** in v1; prune removes reciprocal edges.
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
   `d(c,s) > d(c,ref)`. Worked example in `HNSW-SPEC.md` §7.1.
10. **`validate()`**: no self/duplicate edges, degree caps, layer membership,
    symmetry, entry at `topLayer`, `levelCount` consistency.

No physical delete. If deletion is added later, mark-deleted: keep routing, filter
results, count live vs slot_count. The 20%-delete recall test is not v1.

---

## 6. Measurement protocol

### 6.1 Configuration and build

A configuration is `(dataset, M, efConstruction)`. Build once; measure build time
once; warm up 100 queries; run **5 query trials** against the same build. `efSearch`
sweeps reuse the build. A fresh process is used per configuration, not per trial.

No persistence in v1. If per-trial process isolation is ever required, it waits for a
serialized index (stretch).

The BF baseline is computed once per `(dataset, query set, k)` and cached. It uses the
same distance kernel, compiler flags, thread count, and query batch as HNSW. Do not
rebuild it for every HNSW configuration.

### 6.2 Metrics

| Metric | Definition |
|---|---|
| `R@1/10/100` | mean per-query fraction, tie-aware; 95% query-level bootstrap CI |
| `id_overlap` | mean per-query ID-overlap fraction |
| `perfect_top10` | fraction of queries with all 10 true items found |
| `QPS` | `n_queries / wall_s` per trial; median across trials + min/max |
| `p50/p95/p99` | per-trial percentile of per-query latency; median across trials + range |
| `build_s` | one build, timed separately |
| `inserts_per_s` | `N / build_s` |
| `build_dist_comps` | distance calls during build |
| `rss_peak` / `rss_steady` | `VmHWM` / post-warmup RSS; child process if needed |
| `bytes_per_vector` | total index bytes / N |
| `avg_distance_computations` | mean per query; define counting rule |
| `avg_visited` | mean per query |
| `bf_qps` | exact scan, same query set/kernel/flags/thread |
| `speedup` | `qps / bf_qps` at the SLO point |
| `p95_ratio` | `bf_p95 / hnsw_p95` |
| `dist_ratio` | `N / avg_distance_computations` |

Counting rule: increment `distance_computations` once per distance evaluation between
the query and a stored vector. Visited nodes and distance computations are not the
same number; report both.

### 6.3 Latency methodology

- Per-query samples with `std::chrono::steady_clock`.
- Consume the top-k (checksum) so the optimizer cannot delete the scan.
- Warm up 100 queries; run 5 timed passes over the query set.
- Compute p50/p95/p99 per trial; report the median across trials and the min/max
  range. Do not mix QPS from one trial with p95 from another.
- Pin to one physical P-core (`taskset -c <core>`); record CPU model, SMT sibling,
  core count, RAM, kernel, governor/turbo. Close background load.
- Headline is single-threaded. Multi-thread is a stretch item.
- `-O3 -DNDEBUG`; `-march=native` only for the headline, recorded. Tests use
  ASan/UBSan in a separate build.

### 6.4 SLO rule

Pre-register: **headline = the lowest-latency configuration whose R@10 lower bound
is ≥ 0.90.** Report the actual recall and CI, not a `[0.90, 0.92]` band. Plot the full
Pareto frontier; show dominated points; no interpolation. R@1 is diagnostic.

Do not select the fastest of many grid points on the same queries and then report its
lower bound as if it were unbiased. Either pre-register the grid, split tuning and
reporting queries, or apply a multiplicity correction. For v1, the simplest defensible
rule is: tune on `sift10k`, report on `sift100k` with all 10k queries.

### 6.5 Speedup claim

Report three ratios separately:

- QPS ratio at the SLO point;
- p95 ratio;
- distance-computation ratio (`N / avg_distance_computations`).

Explain why wall-clock speedup is smaller than the distance ratio: brute force
streams memory sequentially, while HNSW random-accesses the graph. Use the same BF
kernel and flags. Do not hardcode "500×"; at 100k, the measured ratio will be
smaller than the old 1M example. If a 1M row is measured, it gets its own labeled
claim.

### 6.6 Output

- `results.csv`: one row per configuration/trial aggregate.
- `bench.json`: the same rows plus provenance, for the site.
- Raw per-query sidecars (IDs/latencies/hits) for recomputing CIs and ties.
- A `manifest.json`: commit, dirty flag, machine, compiler/flags, dataset/query/GT
  hashes, seed, artifacts.
- The exporter refuses `dirty=true` and incomplete rows.

---

## 7. CSV/JSON schema

Minimum CSV columns (exact names can be finalized at S18; bump `schema_v` on change):

```text
run_id, schema_v, git_commit, dirty, timestamp_utc,
dataset, n_base, dim, metric, gt_source,
dataset_sha256, query_ids_sha256, gt_sha256,
n_queries, query_seed, build_seed, k, M, M0, efConstruction, efSearch,
threads, compiler, compiler_version, flags, build_type,
build_s, inserts_per_s, build_dist_comps,
rss_peak_mb, rss_steady_mb, bytes_per_vector, index_size_mb,
r1, r1_lo, r1_hi, r10, r10_lo, r10_hi, r100, id_overlap, perfect_top10,
avg_distance_computations, avg_visited,
qps, p50_ms, p95_ms, p99_ms,
trial_qps_min, trial_qps_max, p95_min_ms, p95_max_ms,
bf_run_id, bf_qps, bf_p95_ms, speedup, p95_ratio, dist_ratio,
cpu_model, cores, smt, ram_gb, kernel, governor, turbo,
limitations
```

Rules:

- Every row carries commit/dirty/hashes/flags/threads.
- `schema_v` is an integer; bump on any change.
- A Python or C++ validator checks required fields/types and refuses blanks.
- Raw sidecars: `queries-<sha8>.csv` with `query_id, hit, rank, latency_us`.

The JSON mirrors the CSV with nested `dataset`, `machine`, `compiler`, and `runs`.
The site reads only the JSON; the CSV is the audit trail.

---

## 8. CI vs release

### 8.1 Per-PR

- Build `hns_core`, `hns_io`, `hns_bench`, `hns_tests`.
- Run unit/invariant tests.
- Run recall on `sift10k` with our own GT. Prefer all 10k queries if cheap; otherwise
  a fixed 1k subset paired against a frozen baseline, floor 0.88, labelled
  `PRELIMINARY`.
- Check deterministic counters (`avg_distance_computations`, `avg_visited`), degree
  bounds, level histogram, L0 reachability, RSS/build sanity.
- No QPS/p95 gate on shared runners; those are noise.

### 8.2 Release

- `sift100k`, all 10k queries, 3 build seeds.
- Build time, RSS, recall, curve diff.
- Block on >0.5 pp recall drop or >10% distance/RSS regression.
- Publish only a pinned run on named hardware with `dirty=false`.

A 10k gate does not certify 100k. Say so explicitly in the README. The release run is
the headline.

---

## 9. Tests

### 9.1 Unit / property

- distance: hand-checked, symmetry, zero vector, NaN policy, float64 reference;
- fvecs/ivecs: truncated, wrong dim, empty, record-size remainder, ID range;
- store: add/get/size/dim, dim mismatch, no reallocation during read, self-distance 0;
- BF: exact vs `sort` on small random, ties by ID, k>N, empty, same as GT generator;
- recall: tie-aware vs ID overlap, hand examples, bootstrap CI, golden CSV.

### 9.2 HNSW

- `validate()` after every insert on N ≤ 200;
- level histogram ~1/M; cap; entry at top; `levelCount`;
- greedy descent on a hand-built line;
- beam matches exhaustive for N ≤ 20;
- insert 0/1/2; connection before promotion; back-edge repair; no self/duplicate;
- prune worked example; two-cluster kill test; fill-to-M flag;
- search edges: empty, k=1, k>N, ef=1, ef>N, duplicate distances, ties;
- full 10k: R@10 ≥ 0.95 point; full 100k: R@10 lower bound ≥ 0.90 (release).

### 9.3 Trace / IO / bench

- `searchTraced` == `search` for 100 queries (IDs + distances);
- event ordering and counters;
- deterministic JSON/CSV; schema validation;
- fake clock percentiles; query-ID hash; RSS parser.

The two-cluster test is permanent. Do not delete it after S12; it is the guard against
the exact bug the project is about.

---

## 10. Memory and performance

### 10.1 100k

| Thing | Size |
|---|---:|
| Base vectors | 51.2 MB |
| L0 links (M0=32) | 12.8 MB |
| L0 degree | 0.2 MB |
| Upper links/degree | <1 MB |
| Levels/entry/counts | ~0.1 MB |
| Visited tags | 0.4 MB |
| Queries (10k) | 5.1 MB |
| GT IDs + distances | 8.0 MB |
| **Total** | **~79 MB** |

### 10.2 1M (optional)

| Thing | Size |
|---|---:|
| Base vectors | 512 MB |
| L0 links | 128 MB |
| L0 degree | 2 MB |
| Upper links/degree | ~8 MB |
| Levels/counts | ~1 MB |
| Visited tags | 4 MB |
| Queries | 5 MB |
| GT | 8 MB |
| **Total** | **~668 MB** |

No duplicate store. Peak under ~800 MB. The first blowup at 10× is duplicate stores,
then per-node `(maxLevel+1)*M0` blocks, then the loader's temporary buffer. Measure
`VmHWM`/`ru_maxrss`; do not guess in the README.

### 10.3 Expected performance (estimates, not claims)

At 100k, brute force is roughly 10× faster than at 1M (same vector count per query,
one-tenth the data). HNSW query time grows sublinearly. The measured speedup at 100k
will be smaller than the old 1M example. Report the measured ratio; do not promise a
number. Build time is dominated by `N * efConstruction * M` distance work; measure it.

---

## 11. Build and tooling

- C++20, CMake ≥ 3.20, Ninja, g++ (installed) or clang++.
- `hns_core` (no I/O), `hns_io` (bytes/artifacts), `hns_bench` (only main),
  `hns_tests`.
- Presets: `dev` (ASan/UBSan), `release` (`-O3 -DNDEBUG`), `release-native`
  (`-march=native`), `ci`.
- Print build type/compiler/flags at startup; put them in every row.
- No raw `new/delete` in core; no `using namespace` in headers; headers
  self-contained.
- `-Wall -Wextra` from S1; `-Werror -Wconversion` later, after the core is stable.
- No `aligned_alloc` claim while using `std::vector<float>`; add a custom allocator
  only if measured.

---

## 12. Stretch (ordered)

1. **1M one-config scale run** — after S22; 1 build, 1k queries for BF speedup, 10k
   for recall; separate row; never a sweep.
2. **Mark-deleted + reinsert** — keep routing, filter results, slot_count; tests.
3. **Showcase fallback** — if S23–S26 slip, ship README + curve + prune PNG and call
   the static site a stretch. This is a fallback, not a scope change.
4. **WASM `siftsmall` playground** — core only, 10k, clearly labelled; 2-session
   timebox.
5. **Persistence snapshot** — after the format is frozen; snapshot only, no WAL;
   corrupt-one-byte test.
6. **Concurrency** — coarse `shared_mutex` wrapper + TSan; only with a real consumer.
7. **Cosine slice** — normalized 10k×384, BF-cosine GT; claim transfer only with both
   rows.
8. **pgvector / FAISS / hnswlib baseline** — same host, matched recall, script-first;
   otherwise literature-only.
9. **KDTree** — 2D or PCA-8 only; document degradation on SIFT-128.
10. **Filtering / range search / quantization** — in-filtering during beam expansion;
    recall-vs-QPS Pareto, not a headline replacement.

---

## 13. Definition of done

- [ ] `siftsmall` BF = 1.0000; `sift10k`/`sift100k` BF vs our GT = 1.0000.
- [ ] `sift100k` HNSW R@10 lower bound ≥ 0.90, 10k queries, 3 build seeds × 5 trials.
- [ ] Full Pareto with ≥6 measured points and CIs; SLO rule pre-registered.
- [ ] `p50/p95/p99`, QPS, build time, RSS, distance computations, bytes/vector.
- [ ] Tests green, including the two-cluster kill test and trace equality.
- [ ] CI recall gate on `sift10k` with our own GT; release run on `sift100k`.
- [ ] README: one command, real numbers, no placeholders, limitations, CPU/RAM.
- [ ] Static showcase: 3 pages, every number maps to `run_id`, `dirty=false`.
- [ ] `DECISIONS.md` matches the implementation; no silent scope creep.
- [ ] 20-minute whiteboard and 60-second demo, no notes.
- [ ] Final tag; optional 1M row clearly separated.

---

## 14. Open questions

1. **Exact `M` default**: start at 16, sweep 16/32. Pick the SLO point on the curve,
   not a blog.
2. **`efConstruction` default**: 200; sweep 100/200. Build time vs recall.
3. **Fill-to-M**: implement the flag, measure on 10k, choose from data.
4. **Trace cap**: 500 nodes / 5 queries; adjust only if the JSON is too large.
5. **1M run**: after S22, only if the 100k release is stable and the build fits the
   time budget. It is a bonus, not a gate.
6. **Mark-deleted**: post-v1 unless a real use case appears.

When an open question is resolved, add a `DECISIONS.md` entry and link it here.

# HNSWhy — Architecture

> **v1 lock:** 100k graded, single-threaded, in-memory, L2, no delete/persistence/
> server/WASM. 1M is one optional scale run after v1. The canonical decisions are in
> `DECISIONS.md`; the algorithm contract is `HNSW-SPEC.md`; the schedule is
> `WORKFLOW.md`.
>
> This file explains the shape and the "why", in plain language. It is not a code
> listing. Session 1 has built the target skeleton; algorithm files arrive with their
> components.

---

## 0. The ten-second version

HNSWhy is **one headless C++ binary** plus **one static site that presents what it
measured**.

You give the binary a vector file and some options. It loads the vectors, builds an
HNSW graph, runs queries, compares against exact search, and writes a CSV plus a
small set of JSON artifacts. The site turns those artifacts into a curve, a query
replay, and a pruning proof. There is no login, no database server, and no API. The
core is like `git` or `grep`; the showcase is a `dist/` folder on a static host.

The claim is narrow:

> "On 100,000 SIFT vectors, this index found **R%** of the true top-10 at **Q**
> queries per second, while exact search did **B** QPS — a measured **S×** speedup
> at that recall, with a 95% interval and a reproducible command."

The site's job is to make the measurement legible, not to produce it. Every number
on the site comes from an exported artifact with a `run_id`.

---

## 1. The shape: headless core, static showcase

The split surprises MERN developers:

| You might expect | What actually happens |
|---|---|
| React page with a search box | Static site reading JSON exported by the benchmark |
| Express route `POST /search` | A CLI flag `--efSearch 50`; there is no API |
| MongoDB storing vectors | One flat `std::vector<float>` in RAM |
| A UI showing results | A UI showing *measured* results, the graph, and the replay |
| A server you deploy | A `dist/` folder on Cloudflare Pages/Vercel |

**Why no backend.** The index needs ~700 MB at 1M, free tiers give 512 MB and sleep,
cold starts are 10–30 s, and a cloud CPU would produce different QPS/latency than the
machine named in the README. Static artifacts keep one source of truth. The numbers
are measured locally, on the machine in `manifest.json`.

**Why a frontend at all.** HNSW is invisible. A terminal table proves the result,
not the mechanism. A query touches a few hundred to a few thousand vectors out of
100,000; the interesting part is *which* ones and *why*. A graph + replay makes that
legible in seconds. That is the interview asset, not a search box.

**The honesty rule.** The showcase never computes a benchmark number. It renders
artifacts produced by `hns_bench`, and every artifact names the commit, the machine,
the dataset checksum, and the exact command. If a number is not in `bench.json`, it
does not appear on the site.

**Optional live search.** A WebAssembly build of `hns_core` on `siftsmall` (10k
vectors, ~5 MB) is a stretch item. It is clearly labelled as a 10k demo, not the
100k native benchmark. It is not in v1.

---

## 2. MERN translation table

| MERN thing | HNSWhy equivalent | Notes |
|---|---|---|
| `npm install` | `pacman -S cmake ninja` | install tools, once |
| `package.json` | `CMakeLists.txt` + `CMakePresets.json` | sources and build rules |
| `npm run build` | `cmake --build --preset release` | compile |
| `node server.js` | `./build/bench --dataset sift100k ...` | run it |
| Express server | **nothing** | static showcase; no API, no cold starts |
| MongoDB collection | `std::vector<float>` in `VectorStore` | 100k × 128 floats, contiguous |
| Mongo's B-tree index | `class HNSW` | the thing we build |
| `db.collection.find(...)` | `index.search(query, k, ef)` | one function call |
| a request | one query vector | 128 floats in, 10 IDs out |
| Jest / Vitest | GoogleTest | `./build/hns_tests` |
| Postman | terminal + `results.csv` | how you inspect output |
| React UI | `web/` (React/TS) reading JSON | showcase, not core |
| `results.csv` | `web/public/data/bench.json` | data contract |
| GitHub Actions | GitHub Actions | same, different commands |
| `.env` | CLI flags | `--M 16 --efSearch 50` |

**Mental shift:** the core collapses into one binary. Instead of a server waiting for
requests, you have a program that reads files, computes, and exits. Requests become
rows in a CSV. The showcase is a separate, static reader of those rows.

---

## 3. The components

```
┌─────────────────────────────────────────────────────────────────────┐
│  NATIVE (C++20)                                                      │
│                                                                      │
│  hns_core (library)                                                  │
│    distance · vector_store · brute_force · hnsw · recall · trace     │
│    no main, no argv, no paths, no ifstream, no JSON, no timing       │
│                                                                      │
│  hns_io (library)                                                    │
│    fvecs/ivecs · CSV · JSON · manifest · checksums                   │
│    no HNSW logic                                                     │
│                                                                      │
│  hns_bench (executable)                                              │
│    CLI · dataset orchestration · runner · RSS · export mode          │
│    the only place with paths, flags, timers, and file writes         │
│                                                                      │
│  hns_tests (executable)                                              │
│    unit · invariants · fixtures · recall gate                        │
│                                                                      │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ CSV + JSON (one-way)
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│  STATIC SHOWCASE (web/)                                              │
│    /        claim · curve · table · method · reproduce footer        │
│    /trace   query-centric graph + replay + results vs GT             │
│    /prune   two-cluster nearest vs heuristic proof                   │
│    no server · no API · no database · no computed headline numbers   │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.1 `hns_core` — the algorithms

| Component | Responsibility | Does not |
|---|---|---|
| `distance` | squared L2; metric-specific functions later | allocate, read files |
| `vector_store` | own one flat float array; dim; size; slot → span | know about graphs |
| `brute_force` | exact top-k over a store view | own vectors, mutate the store |
| `hnsw` | graph, params, insert/search/prune, `validate()` | parse files, print, time |
| `recall` | tie-aware recall, ID overlap, bootstrap CI | know about HNSW internals |
| `trace` | `SearchTrace`, `TraceSink`, `searchImpl<true/false>` | write JSON |
| `rng` | seeded level assignment | global random state |

The core is a library, not a program. It takes spans and IDs, never paths. That is
what makes it testable without a filesystem and reusable by the exporter and a future
WASM build.

### 3.2 `hns_io` — bytes and artifacts

- fvecs/ivecs readers with strict record-size and dimension checks;
- CSV writer for benchmark rows;
- JSON writer for `manifest.json`, `bench.json`, `queries/q0042.json`, `prune.json`;
- sha256 / dataset and query-set checksums;
- deterministic key/array ordering and float rounding.

`hns_io` is not linked by `hns_core`. It is linked by `hns_bench` and `hns_tests`.
A future `hns_wasm` links `hns_core` only.

### 3.3 `hns_bench` — the only `main()`

- parse flags (`--dataset`, `--base`, `--query`, `--gt`, `--M`, `--efConstruction`,
  `--efSearch`, `--k`, `--trials`, `--out`, `--export-dir`, `--trace-queries`);
- load the store and query set through `hns_io`;
- build BF / HNSW once per configuration;
- warm up, run query trials, measure latency/QPS;
- measure build time and RSS;
- ask `hns_io` to write CSV/JSON after timing;
- print a human-readable summary.

The algorithm code never sees `argv`. The benchmark code never contains graph logic.
The exporter never computes a metric.

### 3.4 `hns_tests` — the safety net

- unit tests for distance, store, BF, recall, IO;
- `validate()` after every insert on small random graphs;
- hand-built graph tests for descent and beam;
- the two-cluster prune kill test;
- trace equality (`searchTraced` == `search`);
- the CI recall gate on `sift10k` with our own GT.

### 3.5 `web/` — the static showcase

Vite + React + TypeScript + Tailwind. SVG for the curve, Canvas for the graph,
`d3-scale`/`d3-force` as small utilities, React Router for shareable URLs. Build to
`dist/`; deploy to Cloudflare Pages/Vercel. No Next.js: no server, no SEO
requirement, no request-time data fetching.

The full design is in `SHOWCASE.md`.

---

## 4. Ownership and data model

This is the part that prevents a 512 MB duplicate and a whole class of bugs.

### 4.1 `VectorStore`

```text
VectorStore
  std::vector<float> data_     // size() * dim(), contiguous
  size_t dim_
  size_t size_
  // v1: ID == slot (uint32_t). No unordered_map in the hot path.
```

- `reserve(n)` before loading, so slots do not move.
- `add(span)` validates dim and capacity, appends.
- `vector(slot)` returns `std::span<const float>`.
- ID is the slot index. If external IDs are needed later, add a separate
  `external_id` array; do not put a map in the hot path.
- No `vector<vector<float>>`: 1M small allocations is the fragmentation problem the
  flat store exists to avoid.

### 4.2 Brute force

`BruteForce` is a lightweight view over a store:

```text
search(store, query, k) -> top-k by (distance, id)
```

It does not own vectors and does not implement `add`/`remove`. It is the answer key
and the baseline. The same scan loop is used to generate the 10k/100k ground truth;
that is why the GT generator is not a separate, untested code path.

### 4.3 HNSW

`HNSW` holds:

```text
const VectorStore& store_
std::vector<uint32_t> links0_      // N * M0, fixed stride
std::vector<uint16_t> degree0_
upper-layer adjacency (sparse)
std::vector<uint8_t> level_
std::vector<uint32_t> levelCount_
uint32_t entry_
int topLayer_
RNG state
```

It never copies the vectors. The store is immutable during a build/search; the graph
owns only IDs and links. BF and HNSW see the same slots, so the cross-check is
meaningful.

### 4.4 Interface

The minimal interface is:

```text
SearchableIndex
  search(span<const float> query, size_t k, size_t ef) -> SearchResult
  size() -> size_t
  dim() -> size_t
```

`SearchResult` contains the top-k IDs/distances plus `SearchStats`
(`distance_computations`, `visited`, layer counts). Mutation is not in the base
interface. `HNSW` has `build()` and `insert(slot)` for construction/tests. BruteForce
does not pretend to be mutable.

This resolves the old contradiction: an `Index::add(id, vec)` interface would either
copy the vectors or mutate a shared store with an undefined lock story. The store is
the owner; the indexes borrow.

---

## 5. HNSW design

The full algorithm contract is `HNSW-SPEC.md`. The architecture-level decisions:

### 5.1 Layer 0: fixed-capacity flat adjacency

```
links0_:  N * M0 uint32_t   // one allocation, stride M0
degree0_: N uint16_t
```

- `M0 = 2 * M` (default 32 for M=16).
- Insert writes into the node's free slot; prune swap-erases the last neighbour into
  the hole and decrements degree. O(M0), cache-friendly, no offset table.
- The waste is bounded by the degree cap. That is the price of incrementality.
- CSR is **not** used at layer 0. CSR is a static layout; HNSW insert/back-edge
  pruning mutates existing lists. "How do you insert into CSR?" has no good answer.

### 5.2 Upper layers: sparse

Only about `1/M` of nodes reach layer 1, `1/M²` reach layer 2, etc. Upper layers may
use per-node small vectors (or per-layer slabs). The `vector<vector<...>>` ban is
narrowed to layer 0 and to per-vector float storage; upper-layer fragmentation is
acceptable because there are ~6,250 nodes at layer 1 for 100k/M=16, not 100k.

### 5.3 Graph model

v1 uses a **symmetric** graph: if `u` lists `v`, `v` lists `u`. Pruning removes the
reciprocal edge. This makes `validate()` and the interview explanation tractable.
The invariants are in `HNSW-SPEC.md` §3: no self-loops, no duplicate edges, degree
caps, layer membership, entry at top layer, `levelCount[l]` counts nodes with
`level >= l`, symmetry.

### 5.4 Insert / search / prune, in one paragraph each

**Insert.** Sample a level from a seeded geometric distribution. Greedy-descend from
the old top layer to `level+1` with `ef=1`. At each layer from `min(level, oldTop)`
down to 0, beam-search with `efConstruction`, select diverse neighbours, add
bidirectional edges, and prune any neighbour that overflows (using distances to that
neighbour, not to the new node). Only after connecting may the new node become the
entry point and raise `topLayer`.

**Search.** Greedy-descend from `topLayer` to layer 1 with `ef=1`. At layer 0,
beam-search with `ef_eff = max(efSearch, k)` and return the top k by `(distance, id)`.
Terminate when the closest candidate is worse than the worst result and the result
set is full. Visited state is **per layer**, not one global tag.

**Prune.** Sort candidates by distance to the node being pruned. Keep a candidate if,
for every already-selected neighbour `s`, `d(c, s) > d(c, ref)`. This keeps diverse
directions and rejects redundant same-direction neighbours. The worked example in
`HNSW-SPEC.md` §7.1 shows nearest-only dropping the bridge and the heuristic keeping
it. The two-cluster test is the permanent guard.

### 5.5 What is deliberately not here

No physical delete, no persistence, no `shared_mutex`, no thread-local scratch, no
zero-allocation CI, no strong exception guarantee, no filtering, no quantization.
Those are stretch items. A correct single-threaded graph with a basic guarantee is the
v1 target.

---

## 6. Memory budget

Use roughly 100k and 1M, with measured `VmHWM`/`ru_maxrss` replacing the estimates.

### 100k (graded)

| Thing | Size |
|---|---:|
| Base vectors: 100,000 × 128 × 4 B | 51.2 MB |
| Layer-0 links: 100,000 × 32 × 4 B | 12.8 MB |
| Layer-0 degree: 100,000 × 2 B | 0.2 MB |
| Upper links + degree (sparse, ~N/M nodes) | <1 MB |
| Levels/entry/counts | ~0.1 MB |
| Visited tags (one search scratch) | 0.4 MB |
| Queries: 10,000 × 128 × 4 B | 5.1 MB |
| GT IDs + distances: 10,000 × 100 × 8 B | 8.0 MB |
| **Total** | **~79 MB** |

Peak during load should stay under ~100 MB if the loader reads records directly into
a pre-sized buffer. Do not read the whole file into a `vector<char>` and then copy.

### 1M (optional scale run)

| Thing | Size |
|---|---:|
| Base vectors | 512 MB |
| Layer-0 links: 1M × 32 × 4 B | 128 MB |
| Layer-0 degree | 2 MB |
| Upper links + degree | ~8 MB |
| Levels/entry/counts | ~1 MB |
| Visited tags | 4 MB |
| Queries | 5 MB |
| GT IDs + distances | 8 MB |
| **Total** | **~668 MB** |

Peak under ~800 MB with allocator overhead. The machine has 15 GiB; this fits. But
there is **no second store**: BF and HNSW share the same `VectorStore`. A duplicate
would add 512 MB. The first blowup at 10× scale is duplicate stores, then per-node
`(maxLevel+1) * M0` blocks, then the loader's temporary buffer.

The old 650 MB claim was close but omitted degree/upper/visited/GT. Use the itemized
budget and the measured number. Do not claim 64-byte alignment via `aligned_alloc`
while using `std::vector<float>`; auto-vectorization does not require it. If alignment
matters later, add a custom allocator and measure it.

---

## 7. Measurement architecture

The benchmark is a first-class component, not a script afterthought.

### 7.1 Build once, query many

A configuration is `(dataset, M, efConstruction)`. Build the index once. Measure
build time once. Warm up 100 queries. Run **5 query trials** against the same build.
`efSearch` sweeps reuse the build. The BF baseline is computed once per
`(dataset, query set, k)` and cached.

No persistence in v1, so "fresh process per trial" would rebuild the graph five times
and make sweeps impossible. Use a fresh process per **configuration**, not per trial.
If per-trial process isolation is ever required, it waits for a serialized index.

### 7.2 Metrics

- `R@1`, `R@10`, `R@100`: mean per-query fraction, tie-aware, with 95% query-level
  bootstrap CI. ID-overlap recall reported separately.
- `QPS`: queries / wall time per trial; median across trials plus min/max.
- `p50`, `p95`, `p99`: per-trial percentiles; median across trials plus range.
- `build_s`, `inserts_per_s`, `build_dist_comps`.
- `rss_peak`, `rss_steady`, `bytes_per_vector`, `index_size`.
- `avg_distance_computations`, `avg_visited` (define the counting rule).
- `bf_qps`, `speedup = qps / bf_qps` at the SLO point; also p95 ratio and
  distance-computation ratio.
- Threads = 1. State it in every headline.

### 7.3 Query counts and CIs

10,000 queries for the headline, README, showcase, resume, and SLO point. 1,000 fixed
queries for per-PR smoke/regression, labelled `PRELIMINARY`, paired against a frozen
baseline. At n=1,000, the normal-approximation 95% half-width at p=0.9 is ±1.9 pp;
at n=10,000 it is ±0.6 pp. A 0.90 lower-bound gate at n=1,000 requires ~0.919
observed, so a good 0.92 index passes only about 56% of the time. Do not gate the
headline on 1k.

### 7.4 SLO rule

Pre-register the grid and one rule: **headline = the lowest-latency configuration
whose R@10 query-level bootstrap lower bound is ≥ 0.90.** Tune `M`/`efConstruction`
on `sift10k`; run the pre-registered 100k grid once and report every point. Report
the actual recall and CI, not a `[0.90, 0.92]` band. Plot the full Pareto frontier;
show dominated points too; no interpolation. R@1 is a diagnostic, not a second gate.

### 7.5 CI vs release

- **PR:** build, unit/invariant tests, recall on `sift10k` with our own GT (10k
  queries if cheap, else a paired 1k), deterministic counters, RSS/build sanity. No
  QPS/p95 gate on shared runners.
- **Release:** `sift100k`, 10k queries, 3 build seeds, build/RSS/curve diff; block on
  >0.5 pp recall drop or >10% distance/RSS regression. Publish only a pinned run with
  `dirty=false`.

The full CSV/JSON schema is in `plan.md` §7.

---

## 8. Trace and export boundary

The core exposes:

```text
template <bool Trace>
SearchResult searchImpl(span query, k, ef, TraceSink* sink) const;

SearchResult search(...) const          // Trace = false
SearchTrace searchTraced(...) const     // Trace = true
```

- `Trace=false` compiles out all recording; the timed path never pays for a virtual
  call.
- `Trace=true` records events/deltas: layer enter, node visit, candidate push/pop,
  result update, best change, cumulative distance computations. It does **not** store
  the full beam every step. Cap 3–5 queries and ~500 nodes per query.
- `hns_io` writes JSON. `hns_core` has no file I/O, paths, or JSON.
- Test: for 100 queries, `searchTraced(...).ids == search(...).ids` and distances
  match exactly. If they differ, the trace is lying.
- The query graph uses a `GraphView` interface (`neighbors(slot, layer)`, `level`,
  `entry`, `layer_counts`), not friendship with `HNSW`.
- The prune fixture is a separate 2D test: two clusters, nearest vs heuristic, BFS
  reachability, cluster recall. It is not a SIFT benchmark and must be labelled as
  synthetic.

The exporter runs **after** timed trials. It never changes the timed path.

---

## 9. Build system and file layout

### 9.1 CMake targets

```text
hns_core     STATIC   algorithms; no I/O
hns_io       STATIC   fvecs/ivecs, CSV, JSON, manifest, checksums
hns_bench    EXE      CLI, runner, RSS, export mode
hns_tests    EXE      gtest + fixtures
hns_wasm     optional stretch; hns_core only
```

### 9.2 Presets

| Preset | Use | Flags |
|---|---|---|
| `dev` | local debugging/tests | `-O1 -g -fsanitize=address,undefined -Wall -Wextra` |
| `release` | portable benchmark/CI | `-O3 -DNDEBUG -Wall -Wextra` |
| `release-native` | headline run | `-O3 -DNDEBUG -march=native -Wall -Wextra` |
| `ci` | GitHub Actions | `release` + tests + schema check |

`bench --version` prints the build type, compiler, and flags. Every CSV/JSON row
carries them. Do not enable `-Werror -Wconversion -Wsign-conversion` in the first
weeks; the cast noise will dominate the learning.

### 9.3 File tree (planned; add files as components start)

```text
CMakeLists.txt
CMakePresets.json
include/hnswhy/
  types.h
  distance.h
  vector_store.h
  index.h
  brute_force.h
  hnsw.h
  recall.h
  trace.h
  rng.h
src/core/
  distance.cpp
  vector_store.cpp
  brute_force.cpp
  hnsw.cpp
  recall.cpp
  rng.cpp
src/io/
  fvecs.cpp
  ivecs.cpp
  csv.cpp
  json.cpp
  manifest.cpp
src/bench/
  main.cpp
  cli.cpp
  runner.cpp
  rss.cpp
  export_run.cpp
tests/
  test_distance.cpp
  test_store.cpp
  test_brute.cpp
  test_hnsw_invariants.cpp
  test_search_edges.cpp
  test_prune.cpp
  test_recall.cpp
  test_trace.cpp
  test_io.cpp
  fixtures/
scripts/
  fetch-data.sh
  checksum.sh
web/
```

Keep `hnsw.cpp` as one file until it exceeds ~800 lines; split by operation only if
it helps. Do not create empty files for later sessions. The build skeleton appears at
S1; the algorithm files appear with their components.

---

## 10. Testing strategy

| Seam | Tests |
|---|---|
| Distance | hand-checked values; symmetry; zero vector; NaN policy; squared-L2 vs float64 reference |
| VectorStore | add/get/size/dim; dim mismatch; capacity; no reallocation during read; self-distance 0 |
| BruteForce | exact vs `sort` on small random; ties by ID; k>N; empty; same result as GT generator |
| HNSW graph | `validate()` after every insert on N ≤ 200; degree caps; symmetry; entry/topLayer; levelCount |
| Descent | hand-built line/graph; greedy reaches closest; tiny-N exact |
| Beam | matches exhaustive for N ≤ 20; per-layer visited; termination; ef/k edges |
| Insert | 0/1/2 nodes; connection before promotion; back-edge repair; no self/duplicate |
| Prune | worked example; two-cluster kill test; nearest vs heuristic; fill-to-M flag |
| Recall | tie-aware vs ID overlap; hand examples; bootstrap CI; known fixture |
| Trace | `searchTraced` == `search` for 100 queries; event schema; no trace in timed path |
| IO | truncated/corrupt/wrong-dim/empty fvecs/ivecs; record size; deterministic CSV/JSON |
| Bench | fake clock percentiles; query-ID hash; CSV row validator; RSS parser |

The CI gate is the one that matters: `sift10k` with our own GT, fail if the recall
lower bound drops below 0.90. Do not gate QPS on shared runners.

---

## 11. v1 vs stretch vs non-goals

| Area | v1 | Stretch |
|---|---|---|
| Index | flat store, BF, HNSW insert/search/prune, multi-layer, validate | mark-deleted, physical delete, persistence, concurrency |
| Data | siftsmall proof, sift10k CI, sift100k graded | 1M one config, cosine slice, pgvector, KDTree |
| Metrics | R@1/10/100, CI, QPS, p50/p95/p99, build, RSS, distance comps | multi-thread scaling, perf counters, cold cache |
| Showcase | `/`, `/trace`, `/prune` static | WASM playground, second dataset tab, build replay |
| Ops | local binary, CI build/tests | server, Docker, Python bindings |

**Non-goals:** a live 1M backend, auth/accounts/uploads, a RAG/chat app, a database
product, a 3D graph, a mock search box, a five-page dashboard, a 1M sweep.

---

## 12. What "done" looks like

With placeholders until S22:

```text
$ ./build/bench --dataset sift100k --M 16 --efConstruction 200 \
    --efSearch 50 --k 10 --trials 5 --export-dir web/public/data
loaded 100,000 vectors in <L>s
built HNSW in <B>s (M=16, M0=32, efConstruction=200)
R@10 <R> [<lo>, <hi>]  QPS <Q>  p50 <...>ms  p95 <...>ms  p99 <...>ms
RSS <...>MB  avg distance computations <...>
wrote results.csv, manifest.json, bench.json, queries/q0042.json
```

One command. Real numbers. Reproducible on the named machine. Then:

```text
$ cd web && npm ci && npm run build
$ # deploy dist/ to Cloudflare Pages
```

One static URL. The same numbers, the graph, and the query that produced them. If
the 1M row was measured, it appears in a separate tab and says `N = 1,000,000`; it
never replaces the 100k claim.

---

## 13. Interview questions this architecture answers

1. **Why a flat store?** One allocation, contiguous scans, no 100k small vectors.
2. **Why fixed-capacity L0, not CSR?** HNSW mutates neighbour lists; CSR is static.
3. **Why does the store own the vectors?** BF and HNSW must see the same slots; a
   duplicate store costs 512 MB at 1M and can drift.
4. **Why is BF the answer key?** It is simple enough to verify by hand; the same scan
   generates the ground truth.
5. **Why generate 100k GT?** The shipped key is for all 1M; a subset against it is
   invalid.
6. **Why 100k?** Iteration speed and a valid answer key; 1M is a scale check.
7. **Why build once and query five times?** Isolates build variance from query
   variance; makes sweeps possible without persistence.
8. **Why the diversity heuristic?** Nearest-only can drop the bridge between
   clusters; the two-cluster test proves it.
9. **Why no server?** The claim is measured locally; a cloud CPU would produce
   different numbers and a second source of truth.
10. **What breaks at 10×?** Duplicate stores first, then per-node adjacency blocks,
    then the loader's temporary buffer. The memory table is the answer.

# HNSWhy — SHOWCASE

> The core binary proves the claim. This layer makes the proof legible.
>
> Read `ARCHITECTURE.md` §1 and §8 first. This file is the design for `web/` and the
> small exporter that feeds it. The showcase starts at S23, after the S22 release run.
> It adds no index logic.

---

## 0. The one rule

> **The showcase never computes a benchmark number. It renders artifacts produced by
> `hns_bench`.**

If a number appears on the site, it came from a JSON file written by the C++ binary,
and that file names the commit, the machine, the dataset checksum, and the exact
command. The site has no API, no database, and no way to invent a data point.

Every hero number has a `run_id`. Every chart point is a measured row. The line
between points is a guide, not interpolation. If a value is missing, the UI says
"not measured", not "0".

---

## 1. Why a showcase at all

There are three evaluation moments:

| Moment | Time | What they look at | What wins |
|---|---|---|---|
| Resume / GitHub | 5 seconds | repo name, README top | one striking number + one picture |
| Phone screen | 15 minutes | shared screen | a 60-second tour that shows the mechanism |
| Onsite | 20 minutes | whiteboard | your head, not your laptop |

The terminal table proves the result. It does not show the mechanism. HNSW is
invisible: a query touches a few hundred of 100,000 vectors, and the interesting part
is *which* ones and *why*. A graph and a replay make that visible in seconds.

That is the interview asset. Not a search box.

---

## 2. Why not a live backend

| Constraint | Reality |
|---|---|
| RAM | 100k ≈ 80 MB; 1M ≈ 700 MB |
| Free tiers | typically 512 MB, and they sleep |
| Cold start | 10–30 s; the interviewer closes the tab |
| Cost | ~$7–15/month for a box that stays warm |
| Fragility | a link that is down during the interview is worse than no link |
| Credibility | a cloud CPU produces different QPS/latency than the machine in the README |

The benchmark must run on the machine named in the manifest, because the README claims
that CPU and RAM. A cloud box would produce a second set of numbers, and then the
site and the README would disagree. Static artifacts keep one source of truth.

If you want live search later, compile the same core to WebAssembly and ship
`siftsmall` (10,000 vectors, ~5 MB). That is a stretch item, clearly labelled as a
10k demo, not the 100k native benchmark. It is not in v1.

---

## 3. The site map: three pages

Each page answers one question. About/fine print is a footer, not a route.

```text
/          The claim: headline numbers, recall–QPS curve, table, method, reproduce
/trace     The search: one query's graph + replay + results vs ground truth
/prune     The hard part: why nearest-only pruning fails
```

No `/graph`: the trace page already contains the query-centric graph. No `/about`:
the footer has the commit, machine, flags, dataset checksum, limitations, and
commands. No `/playground` in v1.

### `/` — The claim

- Hero numbers from the frozen release row: `R@10` with CI, `QPS`, `p95`, speedup at
  the SLO point, average distance computations.
- One sentence: "R% of the true top-10 while touching V of 100,000 vectors."
- The recall–QPS curve. One point per measured run. Hover shows `run_id`, the command,
  and the CSV row.
- A filter for `M`, `efConstruction`, `efSearch` that **selects among measured
  points**; it never interpolates.
- A sortable table of all runs, including dominated points.
- A "method" block: dataset, query count, GT source, threads, hardware, flags,
  build time, RSS, limitations.
- A "reproduce" footer: commit SHA, `dirty=false`, dataset/query/GT SHA-256, exact
  command, link to the CSV.

### `/trace` — The search (the centerpiece)

One page, two linked views:

1. **Graph**: a query-centric subgraph — the union of nodes the selected query
   visited, plus their layer-0 neighbours, capped at 500 nodes. Layer tabs
   `L2 / L1 / L0`; hover shows vector ID, level, degree, distance to query; toggle
   "only visited nodes".
2. **Replay**: play / pause / step / speed; a scrubber over the search events. The
   node set grows as the search descends. The candidate beam is highlighted. A side
   panel shows current layer/node, best distance, candidates in the beam, visited
   count, and distance computations.

At the end: HNSW top-10 next to the ground-truth top-10. Matches green, misses red,
plus each true neighbour's rank in the HNSW result. The counter is the thesis:
distance computations vs `100,000`.

The graph and trace are one JSON file (`queries/q0042.json`) so they cannot drift.

### `/prune` — The hard part

The single hardest idea, made visual.

- A synthetic 2D dataset: two separated clusters, ~200 points each. Not SIFT, not
  128-D; 2D is the point.
- Two graphs side by side: "keep nearest M" vs "keep diverse M" (Algorithm 4).
- Animate a BFS from the entry. In the nearest-only graph, the second cluster is
  unreachable; in the heuristic graph, a bridge exists.
- Show **reachability** (`reachable / total`) for both. If you also measure cluster
  recall, label it separately; do not call reachability "recall".
- An inset explains the predicate with actual squared distances: candidate `B` is
  rejected because it is closer to selected `A` than to the node being pruned;
  candidate `D` is kept because it is farther from `A` than from the reference. The
  full worked example is in `HNSW-SPEC.md` §7.1.
- The README embeds this image. It is the first picture an interviewer sees.

**Important:** the old "41% → 99%" was illustrative. The site must show the measured
fixture values, generated by the exporter, with a `run_id`/fixture id. The test
threshold is nearest-only reachability `< 0.5` and heuristic `> 0.9`; the actual
numbers can be different.

---

## 4. The data contract

The C++ exporter writes a small, versioned dataset:

```text
web/public/data/
  manifest.json
  bench.json
  queries/
    q0042.json          # graph + trace + result for one query
    q0007.json
  prune.json
```

### `manifest.json`

```json
{
  "schema": 1,
  "project": "HNSWhy",
  "commit": "<sha>",
  "dirty": false,
  "generated_at": "<utc>",
  "dataset": {
    "name": "sift100k",
    "base": 100000,
    "dim": 128,
    "queries": 10000,
    "ground_truth": "bf_subset",
    "base_sha256": "<sha>",
    "query_ids_sha256": "<sha>",
    "gt_sha256": "<sha>"
  },
  "machine": {
    "cpu": "<model>",
    "cores": 0,
    "ram_gb": 0,
    "os": "<kernel>"
  },
  "compiler": {
    "name": "g++",
    "version": "<version>",
    "flags": "-O3 -DNDEBUG -march=native",
    "build_type": "Release"
  },
  "seed": 42,
  "artifacts": ["bench.json", "queries/q0042.json", "prune.json"]
}
```

### `bench.json`

One entry per measured run. This is the only source of the headline numbers. The
values below are **illustrative placeholders**, not targets; the exporter writes
measured values. Never copy these into the README.

```json
{
  "schema": 1,
  "runs": [
    {
      "run_id": "sift100k-m16-efc200-ef50",
      "command": "<exact command>",
      "dataset": "sift100k",
      "n": 100000,
      "dim": 128,
      "k": 10,
      "M": 16,
      "M0": 32,
      "ef_construction": 200,
      "ef_search": 50,
      "trials": 5,
      "warmup": 100,
      "build_seed": 42,
      "threads": 1,
      "gt_source": "bf_subset",
      "dataset_sha256": "<sha>",
      "query_ids_sha256": "<sha>",
      "gt_sha256": "<sha>",
      "r10": 0.0,
      "r10_lo": 0.0,
      "r10_hi": 0.0,
      "r1": 0.0,
      "r100": 0.0,
      "id_overlap": 0.0,
      "qps": 0.0,
      "p50_ms": 0.0,
      "p95_ms": 0.0,
      "p99_ms": 0.0,
      "trial_qps_min": 0.0,
      "trial_qps_max": 0.0,
      "build_s": 0.0,
      "rss_peak_mb": 0.0,
      "rss_steady_mb": 0.0,
      "bytes_per_vector": 0.0,
      "avg_distance_computations": 0.0,
      "avg_visited": 0.0,
      "bf_run_id": "sift100k-bf",
      "bf_qps": 0.0,
      "speedup": 0.0,
      "commit": "<sha>",
      "dirty": false,
      "limitations": "single-thread; warm cache; local machine; 100k"
    }
  ]
}
```

`speedup` is derived as `qps / bf_qps` from the same query set and the same
kernel/flags/thread count. The site may display it, but labels it "derived". Do not
hardcode 500×; the measured 100k ratio will be smaller than the old 1M example.

### `queries/q0042.json`

One file per traced query, containing graph, events, and result. Shape example only:
the zeros are placeholders, not a real query.

```json
{
  "schema": 1,
  "query": {"id": 42, "gt": [0, 0, 0]},
  "entry": 0,
  "layer_counts": {"2": 0, "1": 0, "0": 100000},
  "nodes": [{"id": 0, "level": 0, "degree": 0}],
  "edges": [{"source": 0, "target": 0, "layer": 0}],
  "steps": [
    {
      "layer": 0,
      "current": 0,
      "dist": 0.0,
      "visited_new": [0],
      "candidates_push": [[0, 0.0]],
      "candidates_pop": [],
      "results_push": [[0, 0.0]],
      "results_pop": [],
      "best": 0,
      "best_dist": 0.0,
      "distance_computations": 0
    }
  ],
  "result": {
    "ids": [0],
    "dists": [0.0],
    "distance_computations": 0
  }
}
```

- Cap the subgraph at ~500 nodes: all visited nodes plus layer-0 neighbours, then a
  small ring. Do not export 100,000 nodes.
- `steps` are deltas. The replay reconstructs the candidate heap and result heap from
  `candidates_push`/`candidates_pop` and `results_push`/`results_pop`. If the UI needs
  a snapshot, emit one at layer boundaries only, never on every step.
- IDs are original vector IDs, not internal slots.
- Floats rounded at export (6 decimals); sorted keys/arrays; deterministic.

### `prune.json`

Shape example; the zeros and single point are placeholders.

```json
{
  "schema": 1,
  "fixture": "two_clusters_2d",
  "points": [{"id": 0, "x": 0.0, "y": 0.0, "cluster": 0}],
  "entry": 0,
  "query": {"x": 0.0, "y": 0.0},
  "variants": {
    "nearest": {
      "edges": [[0, 1]],
      "reachable": [0, 1],
      "reachability": 0.0,
      "cluster_recall": null
    },
    "heuristic": {
      "edges": [[0, 1]],
      "reachable": [0, 1],
      "reachability": 0.0,
      "cluster_recall": null
    }
  }
}
```

`reachability` is BFS reachable / total, not recall. If cluster recall is measured,
it is a separate field with its own definition and query set.

### Schema rules

- `schema` is an integer; bump it on any breaking change.
- `scripts/check-data.mjs` validates required fields and types before the site builds.
  A mismatch fails CI, not the interview.
- `dirty=true` artifacts are refused by the exporter/site build.
- Every hero number maps to a `run_id` and a CSV row; the site has a test that checks
  this.
- If a field is missing, display "not measured"; do not default to 0.

---

## 5. The C++ side

### 5.1 Library boundary (done at S1)

```text
hns_core   algorithms, no I/O, no main
hns_io     fvecs/ivecs, CSV, JSON, manifest, checksums
hns_bench  CLI, runner, RSS, export mode; the only main()
hns_tests  unit/invariant/fixture tests
hns_wasm   optional stretch; hns_core only
```

The fvecs reader lives in `hns_io`, not `hns_core`. It loads the file into a
`VectorStore` and hands the core spans. The core never sees a path.

### 5.2 Exporter CLI

```bash
./build/bench \
  --dataset sift100k \
  --base data/sift/sift_base.fvecs --limit 100000 \
  --query data/sift/sift_query.fvecs \
  --gt tests/fixtures/sift100k/gt.ivecs \
  --gt-dist tests/fixtures/sift100k/gt_dist.fvecs \
  --M 16 --efConstruction 200 --efSearch 50 --k 10 \
  --trials 5 --out results.csv \
  --export-dir web/public/data \
  --trace-queries 42,7,123
```

- `--export-dir` writes `manifest.json`, appends to `bench.json`, and writes the
  traced query files and prune fixture.
- `--trace-queries` runs traced searches **after** the timed trials. The benchmark
  path never pays for instrumentation.
- A separate tiny fixture exports `prune.json`; it does not need SIFT.
- The exporter refuses `dirty=true` and refuses a schema mismatch.

### 5.3 Trace without polluting the benchmark

```text
template <bool Trace>
SearchResult searchImpl(span query, k, ef, TraceSink* sink) const;

SearchResult search(...) const          // Trace = false
SearchTrace searchTraced(...) const     // Trace = true
```

The timed path instantiates `Trace=false`; the compiler removes the sink branches.
The exporter instantiates `Trace=true`. Test: 100 queries, same IDs/distances.

Do not put a virtual `onVisit()` in the hot loop and hope it is free. The
compile-time switch is the design; explain it in the interview.

### 5.4 Query-centric graph export

1. Run the traced search; collect every visited node and its layer.
2. Add each visited node's layer-0 neighbours; cap at ~500 nodes.
3. Emit nodes with original IDs, level, degree; emit edges with the layer.
4. Emit `layer_counts` for the whole index, so the site can show the funnel
   (`100,000 → ~6,250 → ~390 → ~24` for M=16; exact counts from the run).

The result is exactly the part of the index that produced this answer.

### 5.5 The prune fixture

Two Gaussian clusters, ~200 points each, in 2D. Build the graph twice — nearest-only
and Algorithm 4 — and export points, edges, entry, query, reachable set, and
reachability. This is a **test fixture**, not a SIFT benchmark. It is allowed to be
2D because it demonstrates a graph property; label it synthetic.

### 5.6 JSON writer

Write it by hand (~100 lines) or vendor `nlohmann/json` in `hns_io` only.
Recommendation: a small tested writer, because the schema is simple and the core stays
dependency-free. Sort keys/arrays; fixed float rounding; include the seed and
checksums. Do not put JSON in `hns_core`.

---

## 6. The frontend

### 6.1 Stack

| Choice | Why |
|---|---|
| **Vite + React + TypeScript** | you know React; static build; no server |
| **Tailwind CSS** | fast, consistent layout |
| **SVG for the curve** | precise axes; no chart library; small bundle |
| **Canvas 2D for the graph** | 500 nodes at 60 fps; SVG would choke at the cap |
| **`d3-scale`, `d3-force`** | small utilities, not all of D3 |
| **React Router** | shareable URLs like `/trace?query=42` |
| **Cloudflare Pages / Vercel** | free, fast, no cold starts |

No Next.js: no server, no SEO requirement, no request-time data fetching. Vite
produces `dist/`; that is the deployment.

### 6.2 Rendering rules

- The curve is the hero. One chart, clear axes, units, caption with `run_id`.
- The graph is a canvas, not a React tree. Keep node/edge state in refs; do not
  re-render 500 components per frame.
- The replay is driven by a single `step` index; everything else derives from it.
  Play/pause is a `requestAnimationFrame` loop with a speed multiplier.
- Hover uses a linear scan over ~500 nodes; that is fast enough. No quadtree.
- Code-split `/trace` and `/prune`; the landing page loads fast.
- Keyboard controls for play/pause/step; visible focus; `prefers-reduced-motion`.

### 6.3 Layout pipeline

Force layout in the browser at load time is janky and non-deterministic. Precompute:

```text
scripts/layout.mjs
  reads  web/public/data/queries/*.json
  runs   d3-force with a fixed seed (300 ticks)
  writes web/public/data/queries/*.layout.json
```

Run as `npm run data:layout` before `npm run build`. The site draws fixed positions.
Regenerate when the graph changes; commit the layout files or build them in CI.

### 6.4 Visual direction

**Technical report, not SaaS landing page.**

- Near-black background (`#0b0d10`), off-white text, one amber accent (`#f5a524`)
  for HNSW, cyan (`#4cc9f0`) for ground truth, red for misses.
- Monospace for numbers (JetBrains Mono / IBM Plex Mono), sans for prose.
- No hero image, no gradients, no glassmorphism, no stock illustrations.
- Every chart has a caption with the command/`run_id`.
- Motion 150–250 ms; respect `prefers-reduced-motion`.
- Color for layer/state, not decoration. Never encode meaning by color alone; use
  labels/shapes too.
- The overview and table work on a 390 px phone. The graph can say "best on desktop".

The design should feel like a measurement, not a pitch.

### 6.5 Performance and accessibility

- `bench.json` under ~100 KB; each query trace under ~1 MB (gzip); 500-node cap.
- Landing page under ~2 s on a normal connection.
- Lighthouse performance/accessibility reasonable (≥90 where it matters); the graph
  is usable with a keyboard.
- No analytics, no cookies, no API keys, no database.

---

## 7. Build and deploy

```bash
# 1. produce evidence locally, once per frozen run
./build/bench ... --export-dir web/public/data --trace-queries 42,7,123

# 2. validate + build the static site
cd web
npm ci
npm run data:check
npm run data:layout
npm run build

# 3. deploy dist/
# Cloudflare Pages: build command `npm run build`, output `dist`
# or: npx wrangler pages deploy dist
```

CI (GitHub Actions) does **not** run the 100k benchmark. It:

1. builds the core and runs gtest;
2. validates `web/public/data/*.json` against the schema;
3. builds the site;
4. deploys on `main`.

The 100k artifacts are generated locally on the machine named in the manifest. Commit
the small JSON files under `web/public/data/` (the traces are capped at 500 nodes) so
a clean clone and CI can build the site; the dataset itself is not committed. That is
deliberate: the numbers must match the hardware in the README. A `make showcase`
target runs the benchmark, exports, validates, and builds the site. One command, no
manual JSON editing.

---

## 8. Session plan

The core is S1–S22. The showcase is four sessions, S23–S26. Ship is S27–S28.

| # | Goal | Done when |
|---|---|---|
| S23 | Export the evidence: library boundary, `manifest.json`, `bench.json`, `queries/q0042.json`, `prune.json`, schema check | one command produces valid artifacts from the S22 release run |
| S24 | `/` overview: Vite/React/TS, curve, table, method, reproduce footer, deploy | public URL; every hero number maps to `run_id` |
| S25 | `/trace`: query-centric graph + replay, layers, results vs GT, keyboard | the search is legible without narration; 500-node cap |
| S26 | `/prune` + polish: two-cluster visual, reachability/recall labels, README image, responsive | reachability flips on screen; numbers match CSV |

If S23–S26 slip, ship the README + curve + prune PNG and call the static site a
stretch. Do not delay the v1 core for the showcase.

Optional after S28: WASM playground on `siftsmall` (timebox two sessions), a second
dataset tab, a build replay. None are v1.

---

## 9. Definition of done

- A public HTTPS URL that loads in under two seconds on a normal connection.
- Every number on the site is traceable to a JSON row and the commit in
  `manifest.json`.
- A clean clone can regenerate the data and the site with two commands (given the
  datasets).
- The 60-second demo works without opening a terminal.
- The README embeds the prune image and links the site.
- CI validates the data schema and builds the site; it does not fake a 100k run.
- No backend, no API keys, no database, no analytics, no cookies.
- `dirty=false`; the artifacts match the tagged commit.
- Lighthouse performance/accessibility reasonable; the graph is keyboard-usable.

---

## 10. The 60-second demo

Fill every placeholder from `bench.json`; never say a number that is not in the
artifact.

1. **0–10 s — claim.** "Brute force over 100,000 SIFT vectors does about B queries
   per second. I built HNSW from scratch in C++ and measured R% recall@10 at Q QPS —
   a measured S× speedup at that recall."
2. **10–25 s — curve.** "Every point is a real run. Hover one and you get the
   `run_id` and the exact command. Build was B seconds; RSS was M megabytes. Nothing
   here is interpolated."
3. **25–40 s — trace.** "Here is one query. It starts at the top layer, descends
   greedily, then searches a beam of `ef=<efSearch>` at layer 0. It touched V vectors
   — P% of the dataset — and found H of the 10 true neighbours."
4. **40–50 s — prune.** "The hard part was connectivity. Keeping only the nearest
   neighbours disconnects the two clusters; the diversity heuristic keeps a bridge.
   Reachability goes from N1 to N2, and cluster recall from R1 to R2."
5. **50–60 s — close.** "Ground truth is exact scan over the same 100k prefix, and CI
   fails if recall drops below 0.90. It is in-memory, single-threaded, and SIFT-only;
   the next step would be IVF-PQ for memory-bound scale."

If the interviewer asks for more, go to the whiteboard. Do not open the graph
explorer; there isn't one.

**Resume bullet template:**

> Built a from-scratch C++20 HNSW vector index and exact-scan ground-truth harness
> over 100k SIFT vectors — R@10 0.9X [CI], X QPS vs Y QPS exact (Z×) at the SLO
> point, p95 W ms, B s build, M MB RSS; recall-gated CI and a static trace explorer
> that replays each query's graph walk. Demo: `<url>`.

If and only if the 1M row is measured, add "1M scale run." Three numbers maximum;
CPU/RAM and the full table are in the README.

---

## 11. Non-goals

| Not this | Because |
|---|---|
| A live 1M backend | 700 MB, cold starts, cost, numbers that do not match the README |
| Auth, accounts, uploads | nothing to do with the claim |
| A chat/RAG interface | not this project |
| A dashboard with twenty charts | one curve, one table, three visuals is enough |
| A 3D graph | harder to read, not more impressive |
| A mock search box | the entire point is that the data is real |
| A five-page site | `/graph` and `/trace` duplicate; About is a footer |
| WASM in v1 | second toolchain, not the claim |

---

## 12. Risks

| Risk | Mitigation |
|---|---|
| Export format churns | versioned schema + `check-data.mjs` in CI |
| Trace files are huge | 500-node cap, events not full beams, gzip |
| Graph layout is slow/jittery | precompute with a fixed seed at build time |
| Scope creep in the UI | one question per page; cut anything that does not answer it |
| Numbers drift from the README | regenerate artifacts at the final commit; show the SHA |
| WASM eats a week | timebox two sessions; ship the static site without it |
| Site hides a weak result | show limitations and CIs; never replace a measured number |

---

## 13. What this requires from the core

Only two things, both cheap if done early:

1. **Build `hns_core` as a library from S1.** The CLI is a thin shell. This enables
   the exporter, tests, and a future WASM build without a rewrite.
2. **Design the trace hook before search is frozen.** A `template <bool Trace>`
   implementation keeps the timed path clean and makes the replay possible.

Everything else in S1–S22 is unchanged. The showcase starts at S23, after the numbers
exist. **Measure first, show second.**

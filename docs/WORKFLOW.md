# HNSWhy — WORKFLOW

> The practical plan: 28 sessions, 2–3 hours each, with binary checkpoints.
> Read `START-HERE.md` first. `DECISIONS.md` is the lock. `ARCHITECTURE.md` explains
> the shape. `HNSW-SPEC.md` is the hard algorithm.
>
> Status: Session 1 complete; Session 2 is next.

---

## 1. What we are building, in words you already know

You know how a database index works: instead of scanning every document, MongoDB
keeps a B-tree so it can jump to the answer. We are building the same idea for
**vectors**.

A vector is an array of 128 numbers:

```
[0, 16, 35, 5, 32, ...]   // one image
```

Similar images produce similar arrays, so "find similar images" becomes "find the
nearest arrays." The program answers:

> Given an array of 128 numbers, which 10 stored arrays are closest?

| Approach | What it does | MERN analogy |
|---|---|---|
| **Brute force** | Compare the query against all stored vectors. Always correct. Slow. | `.map().sort().slice(0, 10)` over every document |
| **HNSW** | Walk a layered graph of shortcuts, compare against a few hundred. Approximate. Fast. | A database index, but approximate |

We build both. Brute force is the answer key. HNSW is the thing we measure.

### The one picture

HNSW is a graph with layers, like a road network:

```
layer 2:   A ——————————————— D            (highways, very few nodes)
layer 1:   A —— B ——— C ——— D ——— E      (main roads)
layer 0:   A-B-C-D-E-F-G-H-I-J-K-L-M-N    (every node, local streets)
```

To find the nearest node: start at the top, always step to the neighbour that is
closer to the query, drop a layer, repeat. At the bottom, search a wider beam and
return the best 10. Everything else — `M`, `efSearch`, pruning — is about keeping
that network connected and well-shaped.

### The showcase

After the numbers exist, we build a small static site: the recall–QPS curve, one
query's graph walk, and the two-cluster prune proof. No server, no database. It
reads JSON written by the benchmark. The C++ core is still the project; the site
only makes the measurement legible.

---

## 2. Glossary

| Term | Plain meaning | You know it as |
|---|---|---|
| vector | 128 numbers describing one image | an embedding / feature array |
| dim | how many numbers (128) | array length |
| squared L2 | sum of squared differences; smaller = closer | a distance function |
| top-k | the k best results | `.slice(0, k)` after sorting |
| ground truth | the correct answers for a query set | expected output in a test |
| recall@k | of the true k answers, how many did we return? | test accuracy |
| HNSW | the layered graph index | a database index, but approximate |
| layer | one level of the graph | highway → main road → local street |
| M | max neighbours per node above layer 0 | max links per node |
| M0 | max neighbours at layer 0 (2M) | larger link budget for the big layer |
| efConstruction | how hard we look while building | build-time effort |
| efSearch | how hard we look per query | query-time effort |
| QPS | queries per second | throughput |
| p50 / p95 / p99 | typical / slow-tail latency | percentile response times |
| CMake | build configuration for C++ | `package.json` + npm scripts |
| gtest | C++ test framework | Jest / Vitest |
| CI | tests on every push | GitHub Actions |
| fvecs | binary file of float vectors, no header | binary CSV |
| ivecs | binary file of int vectors (answer key) | binary answer key |
| tie | two answers exactly equally close | duplicate equal scores |

---

## 3. What is already verified (so we do not build on sand)

I checked the real SIFT files. Four facts matter:

1. **The format is as expected.** 1,000,000 vectors, 128 floats each, 516 bytes per
   record (4-byte dim + 128 × 4-byte float). Vector 0 against itself is exactly 0.
2. **The shipped answer key is valid only for all 1,000,000 vectors.** A perfect
   brute force on the first 10,000 scores about 0.07 against it, because the true
   neighbours point outside the subset. The old "100k vs shipped key = 1.000" gate
   would fail on flawless code.
3. **`siftsmall` is 10,000 base vectors but only 100 queries**, with its own 100×100
   answer key. It is a loader proof, not a 1,000-query statistical gate.
4. **Exact ties exist.** Two vectors can be equally close; the shipped key picks one
   and we might pick the other. Both are correct. Our recall is distance-threshold
   based, not ID-only.

The dataset ladder and GT rules are in `DECISIONS.md` D2 and `START-HERE.md` §3.
The short version:

- `siftsmall` → shipped GT → loader proof only.
- `sift10k` / `sift100k` → our BF top-100 GT over the exact prefix → CI / graded.
- `sift1m` → shipped GT + BF sample → optional scale run only.
- **Never score a subset against the full-1M key.**

---

## 4. How we work together

Three tiers, not "AI never writes logic":

| Tier | Who writes | What |
|---|---|---|
| **Plumbing** | Agent | CMake/presets, CI, CLI parsing, CSV/JSON writers, React shell, plotting, build errors. You must explain the data flow. |
| **Algorithm** | You, with agent scaffolding | distance, fvecs reader, vector store, brute force, HNSW insert/search/prune, trace hook. Agent writes spec, signatures, failing tests, fixtures; you write bodies; agent reviews. |
| **Ownership** | You | property tests (you state each property), `DECISIONS.md` entries, 20-minute whiteboard, 60-second demo. |

**The rule that matters:** before writing a function, say out loud what it does in
one sentence. If you cannot, stop and figure it out. The goal is a repo you can
defend, not a repo you typed.

**Session rhythm:**

1. Agent explains the one new idea, with a drawing. (10 min)
2. You restate it in your own words. (5 min)
3. You write the function; agent reviews. (60–90 min)
4. Run the checkpoint command. (15 min)
5. Fill one row of `SESSIONS.md`. (5 min)

**Stuck for 45 minutes?** Stop typing. State expected vs actual and the exact error.
Do not grind. **Never pass a red checkpoint.**

CMake is agent-owned in S1: you get a working `hns_core` + `hns_bench` + `tests`
skeleton and read it; S2 adds your first function. You should not debug FetchContent
in week one. Prune is agent-specified, user-written: pseudocode and a worked example,
not code.

---

## 5. The 28-session plan

Each session is 2–3 hours and ends with something that runs. The core is S1–S22;
the showcase is S23–S26; ship is S27–S28. If you only reach S22, you still have a
defensible project.

### Phase 0 — Ruler and skeleton (S1–S2)

| # | Goal | You write | Done when |
|---|---|---|---|
| S1 | Toolchain + targets | nothing; agent scaffolds CMake, presets, CI, `hns_core`/`hns_io`/`hns_bench`/tests | `cmake --preset release`, `cmake --build --preset release`, `ctest --preset release` green; `bench --version` prints Release + flags |
| S2 | Distance + first timing | `squared_l2(a, b, dim)` | 3 hand-checked assertions; synthetic BF p50/p95 printed; nonzero and stable |

### Phase 1 — Trusted data (S3–S6)

| # | Goal | You write | Done when |
|---|---|---|---|
| S3 | Read `fvecs`/`ivecs` | binary reader + checksum | `siftsmall` loads 10,000 × 128; self-distance exactly 0 |
| S4 | Flat store + brute force | `VectorStore`, BF top-k, tie-break | `siftsmall` BF vs shipped GT: tie-aware R@10 = 1.0000 |
| S5 | Build our own answer keys | GT generator (reuse BF) + hashes | `sift100k` top-100 IDs + distances + 10k query IDs committed; self-contained `sift10k` CI bundle (base + 1k queries + GT) committed; BF vs own GT = 1.0000; 100-query float64 sample agrees |
| S6 | Recall evaluator + CI gate | tie-aware recall, ID overlap, bootstrap CI | `sift10k` BF vs own GT = 1.0000; CI build green; query-ID hash recorded |

**S4–S6 are the ruler.** Do not start HNSW until they pass. This is where the old
plan broke; the corrected ladder is the most important part of the project.

### Phase 2 — HNSW (S7–S17)

| # | Goal | Done when |
|---|---|---|
| S7 | Graph model + fixed-capacity L0 + `validate()` | hand-built graph passes all invariants; no self/duplicate; degree caps |
| S8 | Level assignment + entry point | histogram matches ~1/M decay; entry at top; `levelCount` correct |
| S9 | Beam search on a hand-built graph | matches exhaustive top-k for N ≤ 20; per-layer visited works |
| S10 | Insert at layer 0 (NSW): beam-search candidates, bidirectional links, degree cap; no heuristic | 10k NSW R@10 ≥ 0.85 at ef=100; degree ≤ 32 |
| S11 | Nearest-only prune + two-cluster failure | cluster-1 reachability < 0.5 (or recall far below heuristic); recorded evidence |
| S12 | Diversity heuristic + back-edge repair | cluster-1 reachability > 0.9; nearest vs heuristic differ; `validate()` green |
| S13 | Add upper layers to insert + search; HNSW vs NSW ablation | HNSW > NSW or documented "no gain"; 10k R@10 ≥ 0.90 |
| S14 | Tune M / efConstruction / efSearch on 10k | 10k R@10 ≥ 0.95 point; ef sweep; graph code frozen (parameters still sweep in S20) |
| S15 | Trace hook | `searchTraced` IDs/distances == `search` for 100 queries; timed path has no trace |
| S16 | First 100k build | 100k R@10 ≥ 0.90 point; 10k queries if time, else 1k labelled preliminary |
| S17 | Edge cases + invariants + memory | deterministic seed; `validate()` on 10k; no OOB; memory measured; graph code freeze |

This is the hard part. Eleven sessions of graph work is not padding; it is the
project. The two-cluster test in S11–S12 is the best interview story in the repo.

### Phase 3 — Measure (S18–S22)

| # | Goal | Done when |
|---|---|---|
| S18 | Benchmark harness | build once; warmup 100; 5 query trials; p50/p95/p99; QPS; distance comps; RSS; CSV row complete; CI recall gate wired on sift10k (paired 1k) |
| S19 | BF baseline | one BF run per dataset/query set; same kernel/flags/thread; speedup at SLO; cached row |
| S20 | Sweeps on 100k | M=16/32 × efC=200 × ef=10/50/100/200 = 8 points; add efC=100 for the best M only if time; CSV complete |
| S21 | Curve + SLO | full Pareto with CIs; SLO rule pre-registered; no `[0.90,0.92]` band; no interpolation |
| S22 | **v1 core ship** | release run: 100k, 10k queries, 3 build seeds × 5 trials, artifacts schema-valid, README numbers, `dirty=false` |

### Phase 4 — Showcase (S23–S26)

Starts only after S22. It adds no index logic.

| # | Goal | Done when |
|---|---|---|
| S23 | Export evidence | one command writes `manifest.json`, `bench.json`, `queries/q0042.json`, `prune.json`; schema check passes |
| S24 | `/` overview | deployed; hero numbers, curve, table, method, reproduce footer; every number maps to `run_id` |
| S25 | `/trace` | graph + replay in one page; 500-node cap; results vs GT; legible without narration |
| S26 | `/prune` + polish | two-cluster reachability/recall visible; README image; numbers match CSV |

### Phase 5 — Ship (S27–S28)

| # | Goal | Done when |
|---|---|---|
| S27 | Whiteboard + demo | 20 minutes, no notes; 60-second demo; `DECISIONS.md` complete; Loom; resume bullet |
| S28 | Buffer + optional 1M | final tag; or one 1M configuration, labelled separately; no new scope |

**Optional after S28:** WASM playground on `siftsmall`, cosine slice, pgvector
literature row, deletion churn. None are v1. Timebox each; ship without them.

---

## 6. Checkpoints — do not pass without evidence

Every checkpoint is a command, a dataset, a query count, and a threshold. "It looks
right" is not a checkpoint.

| After | Evidence | Must be true |
|---|---|---|
| S1 | `cmake --preset release`, `cmake --build --preset release`, `ctest --preset release`, `bench --version` | Release build; flags printed; tests green |
| S2 | `ctest --preset release -R distance`; synthetic bench | 3 hand-checked distances; p50/p95 nonzero, stable |
| S3 | `bench --dataset siftsmall --load-only` | 10,000 × 128; self-distance 0; checksum printed |
| S4 | `bench --dataset siftsmall --index bf` | tie-aware R@10 = 1.0000; ID overlap reported |
| S5 | `tests/fixtures/` | `sift100k` GT IDs + distances + query IDs + sha256; self-contained `sift10k` CI bundle; BF vs own GT = 1.0000; float64 sample agrees |
| S6 | `bench --dataset sift10k --index bf`; CI | BF vs own GT = 1.0000; recall tests green; query hash recorded |
| S7 | `validate()` on hand-built graph | no self/duplicate; degree caps; layer membership |
| S8 | level histogram | ~1/M decay; entry at top; `levelCount` consistent |
| S9 | beam test N ≤ 20 | matches exhaustive top-k; per-layer visited |
| S10 | 10k NSW benchmark | R@10 ≥ 0.85 at ef=100; degree ≤ 32 |
| S11 | two-cluster nearest-only | cluster-1 reachability < 0.5 or recall far below heuristic |
| S12 | two-cluster heuristic | cluster-1 reachability > 0.9; nearest vs heuristic recorded; `validate()` green |
| S13 | 10k HNSW | R@10 ≥ 0.90; upper-layer gain measured or documented |
| S14 | 10k tuning | R@10 ≥ 0.95 point; ef sweep; graph code frozen (parameters still sweep) |
| S15 | trace equality | 100 queries: same IDs/distances; no trace in timed path |
| S16 | 100k first run | R@10 ≥ 0.90 point; 10k queries or labelled preliminary |
| S17 | edge-case suite | empty, k>N, ef=1, ef>N, ties, dim mismatch; `validate()` 10k; memory measured |
| S18 | 5-trial CSV | build once; warmup 100; p50/p95/p99; QPS; RSS; distance comps; CI recall gate green |
| S19 | BF row | same kernel/flags/thread; BF QPS cached; speedup at SLO |
| S20 | 6–8 points | M × efC × ef sweep; no blank cells |
| S21 | curve + SLO | full Pareto; CIs; pre-registered SLO; no band |
| S22 | release artifacts | 100k / 10k queries / 3 build seeds × 5 trials; schema-valid; README no placeholders; `dirty=false` |
| S23 | exporter | manifest + bench + query + prune; schema check green |
| S24 | `/` deployed | every hero number maps to `run_id`; reproduce footer |
| S25 | `/trace` | graph + replay; 500 nodes; results vs GT; keyboard controls |
| S26 | `/prune` | reachability and recall labelled separately; README image; numbers match CSV |
| S27 | whiteboard + demo | 20 min no notes; 60-sec demo; Loom; `DECISIONS.md` |
| S28 | final tag | optional 1M row labelled; no new scope |

If a checkpoint fails twice in a row, stop and debug the design, not the parameters.
Tuning a broken graph is how people waste a month.

---

## 7. Setup (before Session 1)

CMake and Ninja are the missing tools:

```bash
sudo pacman -S --needed cmake ninja gtest
cmake --version
ninja --version
g++ --version
```

Optional, later:

```bash
sudo pacman -S --needed python-matplotlib cpupower perf
```

Dataset layout (already downloaded, gitignored):

```
data/sift/        # 1M base, 10k queries, shipped answer key
data/siftsmall/   # 10k base, 100 queries, its own answer key
```

Data tooling arrives with the loader:

- S3: `scripts/fetch-data.sh` — download + verify the datasets for a clean clone;
- S3: `scripts/checksum.sh` — print sha256 for base/query/GT;
- S5: `tests/fixtures/` — a self-contained `sift10k` CI bundle (base + 1k queries +
  GT), plus the `sift100k` GT, query IDs, and checksums.

The full datasets are not committed. The fixtures are small and are.

---

## 8. What goes on the resume

After S22, fill from the CSV; do not invent. Shape:

> Built a from-scratch C++20 HNSW vector index and exact-scan ground-truth harness
> on 100k SIFT vectors — R@10 0.9X [CI], X QPS vs Y QPS exact (Z×) at the SLO point,
> p95 W ms, B s build, M MB RSS; recall-gated CI and a static trace explorer.
> Demo: `<url>`.

If and only if the 1M row is measured, add "1M scale run" separately. Do not replace
the 100k claim with a 1M claim that did not run. Three numbers maximum; CPU/RAM and
the full table belong in the README.

The bullet is the least important part. These seven questions are the point:

1. Why does HNSW work? Draw the layers and explain the descent.
2. What does `M` change? What does `efSearch` change? Use your own numbers.
3. Why is brute force the ground truth, and how did you prove the loader is correct?
4. Why does the pruning heuristic beat nearest-only? Draw the two-cluster example.
5. Why is the 100k ground truth generated, and why can't you use the 1M key?
6. Why is the benchmark built once and queried five times? What do p50/p95 mean?
7. Where does every number on the site come from, and how do you reproduce it?

If you can answer these without notes, the project is yours.

---

## 9. Honest expectations

- This is a **systems** project, not a MERN project. The skills are memory layout,
  build tooling, debugging, and measurement discipline. That is why it stands out.
- It will be uncomfortable around S7–S12. The graph will look wrong, recall will be
  low, and the fix will be one line in the prune. That is normal.
- The showcase is four sessions after the core, not a separate project. If you start
  it before the curve exists, you are avoiding the hard part. If you skip it, you
  leave the interview asset on the table.
- 100k is enough. A measured 100k row with a CI, a command, and a trace is stronger
  than a vague "I did 1M."
- If your goal is "something on the resume in four weeks", tell me now. We should
  pick a smaller project. If your goal is "one project I can defend in an interview",
  keep going.

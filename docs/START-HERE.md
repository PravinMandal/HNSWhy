# HNSWhy — START HERE

> Read this first. It is the on-ramp, not the spec. The canonical decisions are in
> `DECISIONS.md`; the architecture is in `ARCHITECTURE.md`; the schedule is in
> `WORKFLOW.md`; the hard algorithm is in `HNSW-SPEC.md`.
>
> **Status: Session 2 complete — build skeleton, squared L2, and the first timing
> smoke test.** Next: Session 3, the SIFT loader. Planning docs live in `docs/`.

---

## 1. What you are building, in plain words

A program that stores **vectors** and finds the closest ones fast.

A vector here is a list of 128 numbers — a fingerprint of an image. Two images that
look similar produce similar lists. The program answers one question:

> "Given this list of 128 numbers, which 10 stored lists are closest?"

You build two ways to answer it:

- **Brute force** — compare against every stored vector. Always correct. Slow.
- **HNSW** — build a layered graph of shortcuts and walk it. Approximate. Fast.

Brute force is the **answer key**. HNSW is the thing under test. The project is not
"an HNSW implementation"; it is **a measurement of an HNSW implementation against
exact search**, with every number reproducible.

The final artifact is:

1. a C++20 binary that loads SIFT, builds the index, runs queries, and writes CSV;
2. a recall–QPS curve and a table with real measured numbers;
3. a static site that replays one query through the graph and shows why nearest-only
   pruning fails;
4. a README that a stranger can reproduce.

The one-sentence version, with placeholders until S22:

> "On 100,000 SIFT vectors, HNSW found **R%** of the true top-10 at **Q** queries
> per second, while exact search did **B** QPS — a measured **S×** speedup at that
> recall, with a 95% interval and a reproducible command."

No number goes in that sentence until it comes from a frozen run.

---

## 2. The correction that matters most

An earlier version of this guide said:

> Load the first 100,000 SIFT vectors, run brute force, compare with
> `sift_groundtruth.ivecs`, and require recall 1.000.

**That gate is invalid as written.** The shipped answer key is defined over **all
1,000,000 vectors**. If you load only 100,000, the true nearest neighbours may live
outside the prefix; a perfect brute-force scan of the prefix scores far below 1.0
against the full-1M key. It is not a loader bug, and it is not a gate a correct
100k implementation can be expected to pass.

The correct ladder is in §3. The 100k gate uses **our own brute-force ground truth
over the exact 100k prefix**. This is the single most important correction in the
plan.

---

## 3. The dataset ladder

| Name | Base vectors | Queries | Ground truth | Use | Gate |
|---|---:|---:|---|---|---|
| `siftsmall` | 10,000 | 100 | shipped `siftsmall_groundtruth.ivecs` | loader proof, smoke | BF tie-aware R@10 = 1.0000; no statistical claim |
| `sift10k` | first 10,000 of 1M | 1,000 or 10,000 | **our BF top-100, IDs + distances** | CI, integration, fast tuning | BF = 1.0000; HNSW point ≥ 0.90 |
| `sift100k` | first 100,000 of 1M | 10,000 | **our BF top-100, IDs + distances** | **graded v1** | BF = 1.0000; HNSW R@10 lower bound ≥ 0.90 |
| `sift1m` | 1,000,000 | 10,000 | shipped `sift_groundtruth.ivecs` + BF sample | optional scale run | BF vs shipped: zero non-tie mismatches; HNSW ≥ 0.90 |

Three rules:

1. **Never score a subset against the full-1M shipped key.** It is invalid.
2. **`siftsmall` is not `sift10k`.** `siftsmall` has only 100 queries; `sift10k` has
   1,000/10,000 queries and our own GT. Do not use `siftsmall` for a 1,000-query
   statistical gate.
3. **Ties are real.** Recall is distance-threshold based: a returned item counts if
   its distance is ≤ the true k-th distance + `1e-6`. Report ID-overlap separately.
   SIFT squared-L2 is integer-valued, so exact ties happen.

A note on what the gates prove: `siftsmall` BF vs its **shipped** GT is the
independent loader/distance check. `sift10k`/`sift100k` BF vs **our own** GT is a
consistency check (the generator reuses the BF loop); the independent checks there
are the 100-query float64 reference and the siftsmall shipped key. Do not present
the own-GT match as independent correctness evidence.

The **self-contained CI bundle** lives under `tests/fixtures/sift10k/`: the first
10k base, a fixed 1k query set, GT IDs + distances, query-ID list, and sha256. The
100k fixture contains GT + IDs + hashes only; the 100k base stays local. The full
datasets stay in gitignored `data/`. A clean clone can run CI from the 10k bundle; a
full benchmark needs the download script (S3).

---

## 4. The v1 lock

This is the scope. Anything not in the "ships" column is a stretch item.

| Area | Ships in v1 | Deferred |
|---|---|---|
| Index | flat store, squared L2, brute force, HNSW insert/search/prune, multi-layer descent, deterministic RNG | delete, persistence, concurrency, filtering, quantization |
| Data | `sift100k` + 10k queries + our GT; `sift10k` for CI; `siftsmall` for loader proof | cosine dataset, 1M sweeps, pgvector, KDTree |
| Metrics | R@1/R@10/R@100, 95% CI, QPS, p50/p95/p99, build time, RSS, distance computations, bytes/vector | multi-thread scaling, perf counters, cold-cache study |
| Safety | unit/invariant tests, tie-aware recall, two-cluster kill test, CI recall gate | physical-delete churn, fuzzing, allocator tests |
| Showcase | `/`, `/trace`, `/prune`; static; 3 pages (planned v1; cut only under a hard deadline) | `/graph`, `/about`, WASM, server, playground |
| Hardware | 1 thread, local machine named in the manifest | 4-thread contention, server |
| Dataset size | **100k graded** | **1M one-config scale run** |

Why 100k? It is 51.2 MB of vectors, builds in minutes, and lets you run a sweep in
one session. 1M is 512 MB and makes every BF pass and every rebuild 10× more
expensive. The algorithm is the same; the measurement is more trustworthy at 100k.
A valid 100k row beats an invalid 1M row. If the 100k run is solid, the optional 1M
row is a bonus, not a second project.

---

## 5. Time budget (honest version)

The plan is **28 sessions × 2–3 hours ≈ 70 hours**, including the showcase and ship
sessions.

| Cadence | Hours/week | Calendar (70 h) | With ~15% slippage |
|---|---:|---:|---:|
| 1 h/day, 7 days | 7 | 10 weeks | 11–12 weeks |
| 3 sessions/week × 2.5 h | 7.5 | 9.3 weeks | 10.5–11 weeks |
| 4 sessions/week × 2.5 h | 10 | 7 weeks | 8 weeks |
| 3 sessions/week × 2 h | 6 | 11.7 weeks | 13–14 weeks |

**Recommended:** 3 sessions per week of 2–2.5 hours, 10–12 weeks. HNSW debugging
needs continuity; 1 hour a day is worse than three longer blocks. If you only have
1 hour a day, expect 11–12 weeks, not 8. If you have a hard 8-week deadline, cut the
showcase and the 1M run, not the data/GT gate.

The old "8–10 weeks at 1 hour/day" claim did not do the arithmetic: 8–10 weeks ×
7 h = 56–70 h, and the 32-session plan was 64–96 h before slippage. This plan is
smaller and the math closes.

---

## 6. Read order

1. **This file** — what and why.
2. **`DECISIONS.md`** — the lock. Read D1–D14 before arguing about anything.
3. **`ARCHITECTURE.md`** — the components, ownership, memory, measurement.
4. **`WORKFLOW.md`** — the 28-session plan and checkpoints.
5. **`HNSW-SPEC.md`** — read §1–§5 before S7; §6–§8 before S9–S12. Not day one.
6. **`SHOWCASE.md`** — read before S23. The site is downstream of the numbers.
7. **`plan.md`** — the deep algorithm/measurement spec. Reference, not a schedule.
8. **`SESSIONS.md`** — the log you fill in after each session.

If two docs disagree, `DECISIONS.md` wins. If a number appears in a doc without a
`run_id`, it is illustrative; replace it with a measured value or delete it.

---

## 7. How we work together

The old rule "AI never writes logic" is right in spirit but too blunt. A beginner
should not debug CMake FetchContent for a week, and a beginner should not be handed
the prune function either. The three-tier contract:

| Tier | Who writes | What |
|---|---|---|
| **Plumbing** | Agent | CMake/presets, CI, CLI parsing, CSV/JSON writers, React shell, plotting, build errors. You must be able to explain the data flow. |
| **Algorithm** | You, with agent scaffolding | distance, fvecs reader, vector store, brute force, HNSW insert/search/prune, trace hook. Agent writes the spec, signatures, failing tests, and fixtures; you write the bodies; agent reviews and quizzes. |
| **Ownership** | You | property tests (you state each property), `DECISIONS.md` entries, the 20-minute whiteboard, the 60-second demo. |

Before writing any function, say out loud what it does in one sentence. If you
cannot, stop. The goal is not "typed every line"; it is "can defend every line."

**Session rhythm:**

1. Agent explains the one new idea, with a drawing. (10 min)
2. You restate it in your own words. (5 min)
3. You write the function; agent reviews. (60–90 min)
4. Run the checkpoint command. (15 min)
5. Fill one row of `SESSIONS.md`. (5 min)

**Stuck for 45 minutes?** Stop typing. Say what you expected, what happened, and the
exact error. Do not grind. **Never continue past a red checkpoint.**

---

## 8. What "done" means

| Milestone | Session | Must be true |
|---|---:|---|
| Trusted ruler | S6 | siftsmall BF = 1.0000; sift10k BF vs our GT = 1.0000; fixtures hashed |
| HNSW works | S14 | sift10k R@10 ≥ 0.95 point; invariants green; two-cluster heuristic passes |
| 100k measured | S16 | sift100k R@10 ≥ 0.90 point; 10k queries or labelled preliminary |
| v1 core ship | S22 | release run, 3 build seeds × 5 trials, full CSV/JSON, README table, no placeholders |
| Showcase | S26 | 3 pages deployed; every number maps to `run_id`; prune proof visible |
| Ship | S28 | 20-min whiteboard, 60-sec demo, Loom, resume bullet, final tag |

If you only reach S22, you still have a defensible project: a measured 100k HNSW
index, a curve, a README, and tests. The showcase is the presentation layer, not the
proof.

---

## 9. Where we are after Session 2

**Sessions 1–2 are complete.** The build skeleton and distance kernel exist:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release

# prints build type, compiler, and the flags that compiled the binary
./build/release/hns_bench --version

# synthetic exact-scan timing; a smoke test, not a benchmark result
./build/release/hns_bench --synthetic-bench
```

Current state:

- `hns_core` — version and squared-L2 distance; no graph yet.
- `hns_io` — build metadata; no dataset readers yet.
- `hns_bench` — `--version` and `--synthetic-bench`; no SIFT benchmark yet.
- `hns_tests` — eight passing tests: five distance, three smoke.
- planning and design docs — in `docs/`.

Next: **Session 3**, the `fvecs`/`ivecs` reader and the `siftsmall` load check.

The three questions to be able to answer before S3:

1. Why does an `.fvecs` record start with a dimension `int`?
2. Why is self-distance exactly `0` a good loader check?
3. Why is `siftsmall` safe to use with its shipped ground truth, while a SIFT prefix is not?

---

## 10. Common traps (read before you debug)

| Trap | Symptom | Fix |
|---|---|---|
| Scoring 100k against the 1M key | BF recall ~0.0X, "loader bug" | Use BF GT over the exact prefix (D2) |
| `-O0` benchmark | QPS 10–50× too low; p50 "stable" but wrong | Release from S1; print flags (D13) |
| Rebuilding 5× per config | Benchmark takes hours; sweeps impossible | Build once, 5 query trials (D10) |
| CSR adjacency | Insert shifts edges; "how do I insert?" | Fixed-capacity L0 (D4) |
| Global visited tag | Recall drops at layer 0; weird misses | Per-layer generation (`HNSW-SPEC` §6.4) |
| Wrong prune reference | Back-edge repair destroys recall | Prune `u` using distances to `u` (§7.4) |
| `ef < k` | Result heap cannot hold k | `ef_eff = max(ef, k)` |
| Physical delete | Graph splits; entry disappears | Mark-deleted stretch only (D7) |
| 1M too early | 512 MB, long builds, invalid GT | 100k first; 1M is one config after S22 |
| Fake 500× | Site says 500×, CSV says 347× | Measured speedup only (D14) |
| Five-page showcase | 2 extra weeks; graph and trace duplicate | 3 pages (D11) |
| "About" page | One more route for no insight | Footer |

---

## 11. Glossary (the short version)

| Term | Plain meaning |
|---|---|
| vector | 128 numbers representing one image |
| dim | how many numbers (128) |
| squared L2 | sum of squared differences; smaller = closer |
| top-k | the k best results |
| ground truth | the correct answers for a query set |
| recall@k | fraction of the true top-k that we returned |
| HNSW | layered proximity graph; approximate nearest-neighbour index |
| layer | level of the graph; layer 0 has all nodes, upper layers are sparse shortcuts |
| M | max neighbours per node above layer 0 |
| M0 | max neighbours at layer 0 (2M by default) |
| efConstruction | beam width while building |
| efSearch | beam width while querying |
| QPS | queries per second |
| p50 / p95 / p99 | median / slow-tail latency |
| fvecs | binary vector file: 4-byte dim, then dim floats, per record |
| ivecs | same, but 4-byte integers (answer key) |
| tie | two vectors at exactly the same distance |

The longer glossary is in `WORKFLOW.md` §2.

---

## 12. The one thing to remember

**The ruler comes before the graph.** S1–S6 exist so that when HNSW looks wrong in
S10, you can tell whether the index is wrong or the ruler is wrong. Skipping the
data/GT gate is how people spend a month tuning a broken graph.

The second thing: **100k measured beats 1M imagined.** Label every claim with `N`,
a query count, a CI, a commit, and a command.

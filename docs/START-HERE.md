# VectorForge / HNSWhy — START HERE

> Read this first. It is the on-ramp, not the spec. The canonical decisions are in
> `DECISIONS.md`; the architecture is in `ARCHITECTURE.md`; the algorithm contract is
> `HNSW-SPEC.md`; the deep reference is `plan.md`.
>
> **Status: planning and merge.** No engine code exists in this repo yet. The
> frontend source exists in the separate VectorForge project and will be imported and
> refactored as `frontend/`. The goal and scope are fixed; the pace is yours.
>
> **Revision note.** This file replaces the old benchmark-only on-ramp. The old lock
> — no server, no persistence, no delete, no RAG, static showcase — is superseded by
> `DECISIONS.md` D1 and D20.

---

## 1. What you are building, in plain words

A user uploads a document. The app splits it into chunks, turns each chunk into a
vector, stores the text and vectors, and lets the user ask a question. The app finds
the chunks closest to the question and asks a local LLM to answer using only those
chunks. The answer comes back with citations.

Under that product is the part that makes the project worth defending: **you write the
vector search engine yourself.** It has two ways to find nearest vectors:

- **Brute force** — compare the query against every stored vector. Always correct.
  Slow.
- **HNSW** — build a layered graph of shortcuts and walk it. Approximate. Fast.

Brute force is the **answer key**. HNSW is the thing under test. The project is not
"an HNSW implementation"; it is **a product with a measured HNSW engine inside it**.

The final artifact is:

1. a React app with Documents, Ask, Map, and Benchmark tabs;
2. a C++ API that stores documents in SQLite and rebuilds the HNSW index on startup;
3. a from-scratch C++20 HNSW engine with tests;
4. a SIFT-100k benchmark with a recall–QPS curve and real numbers;
5. a README a stranger can reproduce.

The one-sentence version, with placeholders until the release run:

> "VectorForge answers questions over uploaded documents using a from-scratch HNSW
> index. On 100,000 SIFT vectors, the same engine found **R%** of the true top-10 at
> **Q** queries per second, while exact search did **B** QPS — a measured **S×**
> speedup at that recall, with a reproducible command."

No number goes in that sentence until it comes from a frozen `bench.json`.

---

## 2. The mental model

```text
Upload
  text -> chunks -> Ollama embeddings -> SQLite (text + vectors)
                                      -> HNSW (in memory)

Ask
  question -> embedding -> HNSW search -> nearest chunks
           -> prompt with numbered sources -> Ollama answer
           -> answer + citations

Restart
  SQLite is the source of truth -> reload chunks -> rebuild HNSW

Benchmark
  SIFT-100k -> hns_bench -> bench.json -> Benchmark tab
```

Three boundaries to keep straight:

| Boundary | Who owns it |
|---|---|
| Text, documents, chunks, prompts, citations | product layer |
| Slots, floats, distances, graph links | engine layer |
| SIFT numbers, parameters, run metadata | benchmark CLI |

The engine does not know what a document is. The product does not know how HNSW
prunes. The benchmark uses the same engine library as the server, so the number is
about the code the product actually runs.

The old project was a headless benchmark with a static showcase. The new project is a
product with the benchmark as one tab. That is a bigger surface. It is also the only
version that gives a stranger something to use and a recruiter something to check.

---

## 3. What ships in v1

| Area | Ships in v1 | Deferred |
|---|---|---|
| Product | upload/paste, list, delete, ask, citations, 2D map, benchmark tab | auth, multi-user, PDF, agents, Graph RAG |
| Persistence | SQLite source of truth; HNSW rebuilt on startup | graph snapshot, physical compaction |
| Engine | flat store, squared L2, brute force, HNSW insert/search/prune, directed graph, mark-deleted | concurrency, filtering, quantization, range search |
| Evidence | SIFT-100k, 10k queries, 3 timed passes per curve point, `bench.json` | 1M sweep, multi-metric, KD-tree, cosine index |
| Showcase | the product UI itself | static site, 3D map, trace-replay page |
| Ops | documented local run, documented Ollama setup | public deployment, Docker, auth gate |
| Tests | unit/invariant, two-cluster, mark-delete, persistence, API | fuzzing, allocator tests, TSan |

Why 100k? It is 51.2 MB of vectors, builds in minutes, and lets you iterate. 1M is
512 MB and makes every brute-force pass and rebuild 10× more expensive. A valid 100k
row beats an invalid 1M row. If the 100k run is solid, the optional 1M row is a bonus,
not a second project.

**The honest risk:** the merge is more work than the old benchmark-only plan. If you
finish only the engine, you have a measured index but no product. If you finish only
the product, you have a RAG wrapper with no depth. The mitigation is not optimism; it
is the cut list in `DECISIONS.md` D20 and the order in §6: engine first, product shell
second, benchmark third, polish last.

---

## 4. The dataset ladder

| Name | Base vectors | Queries | Ground truth | Use | Gate |
|---|---:|---:|---|---|---|
| `siftsmall` | 10,000 | 100 | shipped `siftsmall_groundtruth.ivecs` | loader proof, smoke | BF tie-aware R@10 = 1.0000; no statistical claim |
| `sift10k` | first 10,000 of 1M | 1,000 | **our BF top-100, IDs + distances** | CI, integration, fast tuning | BF = 1.0000; HNSW point ≥ 0.90 |
| `sift100k` | first 100,000 of 1M | 10,000 | **our BF top-100, IDs + distances** | **graded v1** | BF = 1.0000; HNSW R@10 ≥ 0.90 |
| `sift1m` | 1,000,000 | 10,000 | shipped `sift_groundtruth.ivecs` + BF sample | optional scale run | BF vs shipped: zero non-tie mismatches; HNSW ≥ 0.90 |

Three rules:

1. **Never score a subset against the full-1M shipped key.** It is invalid.
2. **`siftsmall` is not `sift10k`.** `siftsmall` has 100 queries; `sift10k` has 1,000
   and our own GT.
3. **Ties are real.** Recall is distance-threshold based: a returned item counts if
   its distance is ≤ the true k-th distance + `1e-6`. Report ID-overlap separately.

A note on what the gates prove: `siftsmall` BF vs its **shipped** GT is the independent
loader/distance check. `sift10k`/`sift100k` BF vs **our own** GT is a consistency check
(the generator reuses the BF loop); the independent checks there are the 100-query
float64 reference and the siftsmall shipped key. Do not present the own-GT match as
independent correctness evidence.

The self-contained CI bundle lives under `tests/fixtures/sift10k/`: the first 10k
base, a fixed 1k query set, GT IDs + distances, query IDs, and SHA-256. The 100k
fixture contains GT + IDs + hashes only; the 100k base stays local. Full datasets stay
in gitignored `data/`.

---

## 5. What "done" means

| Outcome | Must be true |
|---|---|
| Product works | upload/paste, list, delete, ask, citations, map, benchmark tab |
| Persistence works | restart preserves documents and search results; delete survives restart |
| Engine works | BF = answer key; HNSW recall gate; directed graph; mark-deleted; `validate()` |
| Tests green | two-cluster, mark-delete, persistence/restart, API with fake Ollama |
| 100k measured | `sift100k` R@10 ≥ 0.90, 10k queries, 3 timed passes per curve point |
| Artifact exists | `bench.json` with commit, hashes, flags, machine, trials |
| README | short documented setup, real numbers, limitations, no placeholders |
| Demo | 60 seconds, no code edits, no fake data |
| Docs | decisions, architecture, spec, plan, product doc match the code |

If only the engine works, the project is a measured index without a product. If only
the product works, it is a RAG wrapper without depth. v1 is the pair.

**The worth-it test:** at the end, what exists that did not before, and can you defend
it? Not "is this top-class." Existence plus defense.

---

## 6. How we work together

The old rule "AI never writes logic" is right in spirit but too blunt. A beginner
should not debug CMake for days, and a beginner should not be handed the prune function
either. The three-tier contract:

| Tier | Who writes | What |
|---|---|---|
| **Plumbing** | Agent | CMake/presets, CI, CLI parsing, fvecs/JSON writers, React shell, plotting, build errors. You must be able to explain the data flow. |
| **Algorithm** | You, with agent scaffolding | distance, vector store, brute force, HNSW insert/search/prune, mark-deleted, SQLite rebuild. Agent writes the spec, signatures, failing tests, and fixtures; you write the bodies; agent reviews and quizzes. |
| **Ownership** | You | property tests (you state each property), `DECISIONS.md` entries, the 20-minute whiteboard, the 60-second demo. |

Before writing any function, say out loud what it does in one sentence. If you cannot,
stop. The goal is not "typed every line"; it is "can defend every line."

**Build order.** Dependency-driven, not date-driven:

1. trusted data and the brute-force ruler;
2. the engine on small fixtures;
3. the product flow with a fake Ollama;
4. the real Ollama integration;
5. the SIFT benchmark and `bench.json`;
6. the Benchmark tab and deploy/demo polish.

**Stuck for 45 minutes?** Stop typing. Say what you expected, what happened, and the
exact error. Do not grind. **Never continue past a failed gate.**

---

## 7. Common traps

| Trap | Symptom | Fix |
|---|---|---|
| Scoring 100k against the 1M key | BF recall ~0.0X, "loader bug" | Use BF GT over the exact prefix (D3) |
| `-O0` benchmark | QPS 10–50× too low; p50 "stable" but wrong | Release from the start; print flags (D15) |
| Rebuilding per trial | Benchmark takes too long; sweeps impossible | Build once, 3 query trials (D4) |
| CSR adjacency | Insert shifts edges; "how do I insert?" | Fixed-capacity L0 (D5) |
| Global visited tag | Recall drops at layer 0; weird misses | Per-layer generation (`HNSW-SPEC` §6.4) |
| Wrong prune reference | Back-edge repair destroys recall | Prune the overflowing node using distances to itself (§7.4) |
| `ef < k` | Result heap cannot hold k | `ef_eff = max(ef, k)` |
| Physical graph delete | Graph splits; entry disappears | Mark-deleted in memory (D8) |
| Assuming symmetry | Reciprocal cleanup code; one-way edges lost | Directed graph; no symmetry check (D10) |
| Filtering deleted nodes out of traversal | Graph disconnects; live nodes disappear | Traverse tombstones, filter only results |
| Delete-all then insert | New node has no live candidates; queries miss it | Insert beam includes tombstones; query filters them |
| SQLite and graph drift | Wrong citations or missing results | SQLite source of truth; rebuild on startup (D9) |
| Fake embeddings | Map looks nice; search is nonsense | Real Ollama embeddings only |
| KD-tree at 128/768D | Slow, complex, no benefit | No KD-tree; one HNSW path (D14) |
| 3D map / static site | Days of polish, no new proof | 2D map; product UI is the showcase (D12, D14) |
| 1M too early | 512 MB, long builds, invalid GT | 100k first; 1M is one config after v1 |
| Live benchmark in API | Network noise; second source of truth | CLI benchmark; UI reads `bench.json` (D11) |
| "Production-grade" claim | README says more than the code does | Say what is true: portfolio-grade v1 |
| Only happy-path tests | Demo fails on Ollama down, bad file, restart | Error paths and restart test |

---

## 8. Glossary (the short version)

| Term | Plain meaning |
|---|---|
| vector | a list of numbers representing text or an image |
| dim | how many numbers (128 for SIFT, 768 for a typical embedding model) |
| squared L2 | sum of squared differences; smaller = closer |
| cosine | angle-based similarity; on unit-length vectors, same ranking as L2 |
| top-k | the k best results |
| ground truth | the correct answers for a query set |
| recall@k | fraction of the true top-k that we returned |
| HNSW | layered proximity graph; approximate nearest-neighbour index |
| layer | level of the graph; layer 0 has all nodes, upper layers are shortcuts |
| M | max neighbours per node above layer 0 |
| M0 | max neighbours at layer 0 (2M by default) |
| efConstruction | beam width while building |
| efSearch | beam width while querying |
| QPS | queries per second |
| p50 / p95 / p99 | median / slow-tail latency |
| fvecs | binary vector file: 4-byte dim, then dim floats, per record |
| ivecs | same, but 4-byte integers (answer key) |
| embedding | the vector Ollama returns for a chunk or question |
| chunk | a piece of a document, small enough to retrieve |
| RAG | retrieval-augmented generation: retrieve chunks, then answer |
| citation | the chunk ID/title shown as the source of an answer |
| SQLite | the file database that stores documents and embeddings |
| Ollama | the local model server for embeddings and generation |
| PCA | projection of high-dimensional vectors to 2D for the map |

---

## 9. Read order

1. **This file** — what and why.
2. **`DECISIONS.md`** — the lock. Read D1–D20 before arguing about anything.
3. **`ARCHITECTURE.md`** — components, data model, API, memory, tests.
4. **`HNSW-SPEC.md`** — read §1–§6 before writing the graph; §7–§8 before the prune
   and delete. Not day one.
5. **`plan.md`** — the deep product/algorithm/measurement reference.
6. **`SHOWCASE.md`** — the product UI, demo script, and what not to build.

If two docs disagree, `DECISIONS.md` wins. If a number appears in a doc without a
`run_id`, it is illustrative; replace it with a measured value or delete it.

---

## 10. The one thing to remember

**The ruler comes before the graph. The product comes before the showcase.**

The data/GT gate exists so that when HNSW looks wrong later, you can tell whether the
index is wrong or the ruler is wrong. The product-first order exists so that the demo
is real before the polish starts. Skipping either is how people spend a month tuning a
broken graph or building a beautiful page with no working search.

The second thing: **100k measured beats 1M imagined.** Label every claim with `N`, a
query count, a commit, and a command.

# HNSWhy — START HERE

Plain-language build guide. Read this first. `plan.md` is the engineering spec —
it is correct, it is dense, and you do not need it on day one. This file tells you
what to actually do, in order, assuming you have never built anything in C++ outside
of DSA practice.

---

## 1. What you are building, in plain words

A program that stores **vectors** and finds the closest ones fast.

A vector here is just a list of 128 numbers. Think of it as a point in 128-dimensional
space — a fingerprint of an image. Two images that look similar produce similar lists.

The program answers one question, millions of times:

> "Given this list of 128 numbers, which 10 stored lists are closest to it?"

Two ways to answer it:

- **Brute force** — compare against all 1,000,000 stored lists. Always correct. Slow.
- **HNSW** — build a graph of shortcuts, compare against ~200 of them. Correct about
  93% of the time. Fast.

You build both. Brute force is the **answer key** that proves HNSW is right.
The final deliverable is one sentence with real numbers in it:

> "Found 93% of true nearest neighbours while checking 0.02% of the data,
> at 9,000 queries per second, on my own machine."

That sentence cannot be faked, which is why it's worth building.

---

## 2. What you need to learn first — and it is less than it looks

You already know, from 600+ DSA problems in C++: arrays, `vector`, structs, pointers,
loops, recursion, adjacency lists, basic STL. **That is enough to start.** Do not
re-learn C++.

You need these six ideas. Roughly 6 hours total, spread across the first week:

| Idea | What it actually means | Time |
|---|---|---|
| Squared distance | For each of 128 numbers, subtract, square, add. Smaller = closer. No square root needed — it doesn't change the order. | 30 min |
| Nearest-neighbour search | Find the k smallest distances. `nth_element` does it without full sorting. | 30 min |
| Recall@k | Of the k true nearest, how many did you find? 9 of 10 → 0.90. | 15 min |
| Graph + layers | HNSW is a graph where each node points at some neighbours. Layer 0 holds everything; upper layers are sparse shortcuts. You start at the top, walk greedily down, then search properly at the bottom. | 2 h (draw it) |
| p50 / p95 | Sort all query times. p50 = middle one. p95 = the slow one 95% are faster than. Report both; the average hides stalls. | 20 min |
| CMake | A text file listing your source files. You run it once; it produces a `make` file; `make` builds. Nothing more. | 45 min |

Everything else in `plan.md` — Wilson intervals, CSR adjacency, TSan, shared_mutex —
you learn **in the session where it appears**, not upfront.

---

## 3. Your actual level — for any agent reading this

State this to the building agent in your first message. Do not assume it knows.

- **Knows:** JavaScript, Node, Express, MongoDB, React basics. C++ at competitive-DSA
  level — comfortable with `vector`, pointers, structs, recursion, STL containers.
- **Does not know:** CMake, C++ project layout, RAII / move semantics in depth,
  templates, multithreading, benchmarking methodology, HNSW, any systems tooling.
- **Reads code much faster than writes it.** Explain before asking for code. Draw
  the structure before naming the variables.

---

## 4. Division of labour (this replaces "AI never writes logic")

The strict rule in `plan.md` assumes an expert. Adjusted for your level:

| Who | What |
|---|---|
| **Agent writes, no explanation needed** | `CMakeLists.txt`, `.gitignore`, CI yaml, matplotlib plotting, CSV writer, CLI arg parsing, README skeleton |
| **You write, agent explains first** | `squared_l2`, the flat vector store, brute force, HNSW insert / search / prune / remove, the fvecs reader |
| **You write alone** | Tests that encode one property you can state in a single sentence |

**The one rule that matters:** before writing any function, say out loud what it
does in one sentence. If you can't, you don't understand it yet and you must not
commit it. This is the whole difference between this project and the 27,000-line
AI dumps in your other repos.

---

## 5. Session plan — 24 sessions, 4 hours each

Each session ends with something that runs. Never a session that ends in "I read
a lot today."

### Phase 0 — Setup (2 sessions)

**S1 — Toolchain.**
Install `cmake` and `python3-matplotlib` via pacman. Build a hello-world with CMake.
Learn: what `cmake ..` does, what `make` does, why `build/` is a separate folder and
is gitignored. **Done when:** `./build/hello` prints text.

**S2 — Distance function.**
Write `float squared_l2(const float* a, const float* b, size_t dim)`.
Test it by hand: two vectors you can compute on paper, then assert. Then a vector
against itself → 0. **Done when:** three hand-checked assertions pass.

### Phase 1 — Brute force and real data (3 sessions)

**S3 — Brute force on random data.**
Generate 10,000 random vectors. Find the 10 closest to a query by checking all.
Time 1,000 queries with `std::chrono`. Print p50 and p95. Understand why you print
two numbers, not an average. **Done when:** p50 is nonzero and stable across runs.

**S4 — Load real data.**
`curl -O ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz` (160 MB), extract, and
write a reader that loads the first 100,000 records from `sift_base.fvecs`.
Each record = 4-byte integer saying 128, then 128 × 4-byte floats = 516 bytes.
**Done when:** you print vector 0's first five numbers and its distance to itself is
exactly 0.

**S5 — Validate the whole pipeline.**
Run brute force on those 100,000 vectors against the provided answer key
(`sift_groundtruth.ivecs`). Recall must be **exactly 1.000**. It is literally the
answer key checking itself. If it isn't 1.000, you have a parsing bug — stop and fix
it. **This is the most important checkpoint in the project.** Nothing downstream is
trustworthy until it passes.

### Phase 2 — Build the graph (6 sessions)

**S6 — Levels.** Each new node picks a level randomly, usually 0, rarely higher,
decaying geometrically. Draw 20 nodes on paper with their levels. **Done when:**
you can explain the decay with a number from your own run.

**S7 — Insert into an empty index.** 0 nodes, then 1, then 2. Draw the resulting
graph by hand and print it from code. Do not proceed until both drawings match.

**S8 — Search at the bottom layer.** A "beam search": keep a list of the best few,
expand their neighbours, stop when nothing improves. Test on 2, 10, 100 nodes.

**S9 — Link both ways.** When A links to B, B must link back. Cap the number of
neighbours. When a node exceeds the cap, decide who to drop.

**S10 — The pruning heuristic.** This is the hard one. Do not just keep the nearest
neighbours — that creates clusters that can't be reached from outside. Keep
neighbours that point in *different directions*. `plan.md` §4 rule 3 has the exact
algorithm; `docs/prune.md` has the pseudocode. **Done when:** on two separated
clusters, nearest-only gives recall < 0.5 and the heuristic gives > 0.9.

**S11 — Full index at 10k.** Insert all 10,000. Recall@10 against brute force should
be ≥ 0.95. **Done when:** ≥ 0.95. If it isn't, you're guessing parameters instead
of debugging — stop and find the bug.

### Phase 3 — Measure it (4 sessions)

**S12 — efSearch.** How many candidates to hold while searching. Sweep 10, 50, 100,
200. Plot recall vs ef. Watch recall rise and speed fall.

**S13 — Greedy descent.** Walk down the upper layers before searching the bottom.
Measure with and without. **Done when:** you have a number for what descent buys.

**S14 — Sweep M and efConstruction.** M = neighbours per node (memory). efConstruction
= how hard it looks while building (build time). Two M values × three ef values.

**S15 — The curve.** Plot recall@10 on one axis, queries-per-second on the other.
This PNG is the project. Everything else exists to make it true.

### Phase 4 — Prove it (4 sessions)

**S16 — Unit tests.** Distance correctness, insert-then-find, empty index, k > n.
**S17 — Deletion.** Remove a node, then search again. Reinsert it, confirm found.
**S18 — Cluster test.** The two-cluster test from S10, in CI, as a permanent guard.
**S19 — CI.** GitHub Actions: build, run tests, fail if recall@10 < 0.90 on SIFT-10k.
**Done when:** a red CI run blocks you from merging your own change.

### Phase 5 — Ship (5 sessions)

**S20 — Real benchmark harness.** Warmup, 5 trials, fresh process, RSS memory,
CSV output.
**S21 — Full SIFT-100k run.** Fill in every CSV column. No blanks.
**S22 — README.** The table, the curve, your CPU and RAM, the exact flags.
Zero `X` or `Y` placeholders.
**S23 — Whiteboard script.** No notes: draw the layers, explain M → memory,
efConstruction → build time, efSearch → query time, using your own numbers.
**S24 — Loom video + DEMO.md + resume line.**

---

## 6. Checkpoints — stop and fix if you miss one

| After | Must be true |
|---|---|
| S1 | `./build/hello` runs |
| S4 | 100k vectors loaded, self-distance is 0 |
| S5 | brute-force recall **exactly 1.000** |
| S11 | HNSW recall@10 ≥ 0.95 on 10k |
| S15 | curve exists with ≥ 3 ef and ≥ 2 M values |
| S19 | CI green and blocking |
| S24 | README has real numbers, no placeholders |

**Off-track signals, in order of severity:**
1. No runnable binary by end of S4 → the toolchain is fighting you; ask for help, don't grind.
2. Recall below 0.90 at S14 and you're twiddling M and ef → it's a bug, not tuning.
3. More than 2 sessions on tooling → cut something.
4. You cannot explain a committed function in one sentence → revert it.

---

## 7. Glossary

| Term | Plain meaning |
|---|---|
| **vector** | A list of 128 numbers representing one image |
| **dim / dimension** | How many numbers in the list (128 here) |
| **L2 / Euclidean distance** | Square each difference, add them up. Smaller = more similar |
| **k** | How many results to return (usually 10) |
| **brute force** | Compare against everything. Correct, slow |
| **approximate** | Compare against a subset. ~93% right, fast |
| **HNSW** | The graph structure that makes the subset choice smart |
| **layer** | A level of the graph. Layer 0 = everything, upper = shortcuts |
| **recall@k** | Of the k true nearest, the fraction you found |
| **QPS** | Queries per second |
| **efSearch** | How hard to look per query. Higher = more accurate, slower |
| **efConstruction** | How hard to look while building. Higher = better graph, slower build |
| **M** | Max neighbours per node. Higher = more memory, usually better recall |
| **ground truth** | The known-correct answers shipped with the dataset |
| **fvecs** | File format: raw float vectors back to back, 4 + dim×4 bytes each |
| **ivecs** | Same but integers — this is the answer key format |
| **SIFT** | The standard public benchmark dataset (1M vectors, 128 dims) |

---

## 8. The one thing to remember

Session 5 is the gate. If brute force doesn't score exactly 1.000 against the shipped
answer key, every number after it is fiction. Do not skip it, and do not "fix" it by
tuning anything else.

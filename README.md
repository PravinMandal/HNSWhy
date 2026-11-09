# VectorForge

A RAG app backed by a from-scratch C++20 HNSW vector engine.

> **Naming:** the repository directory is still `HNSWhy`; the shipped product is
> **VectorForge**, and **HNSWhy** is the engine inside it.

> **Status: planning and merge.** The engine has no code yet. The product frontend
> exists in the separate VectorForge project and will be imported and refactored as
> `frontend/`. The results table stays `TBD` until a frozen, reproducible run exists.
> The old HNSWhy static showcase is retired; the product UI is the showcase.

---

## What it is

VectorForge lets a user upload documents, ask questions, and get answers with
citations. Under the product is **HNSWhy**, a from-scratch HNSW engine: a flat vector
store, exact brute force, a layered proximity graph, mark-deleted nodes, tests, and a
SIFT-100k benchmark.

The point is not "another RAG wrapper" and not "another HNSW implementation." The point
is the pair:

- a product a stranger can use;
- an engine whose quality is measured, not asserted.

The one-sentence claim, with placeholders until the release run:

> "VectorForge answers questions over uploaded documents using a from-scratch HNSW
> index. On 100,000 SIFT vectors, the same engine found **R%** of the true top-10 at
> **Q** queries per second, while exact search did **B** QPS — a measured **S×**
> speedup at that recall, with a reproducible command."

Why 100k? It is 51.2 MB of vectors, builds in minutes, and uses a ground truth
generated over the exact prefix. SIFT-1M is 512 MB and makes every sweep and every
brute-force pass 10× more expensive. A valid 100k row beats an invalid 1M row. The 1M
run is an optional one-configuration scale check after v1, not a second project.

---

## Results

| Dataset | N | Queries | R@10 | QPS | p95 | Build | RSS | Exact QPS | Speedup |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT-100k | 100,000 | 10,000 | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| SIFT-1M (optional) | 1,000,000 | 10,000 | TBD | TBD | TBD | TBD | TBD | TBD | TBD |

The nearest-only vs diversity-heuristic ablation is a secondary experiment, not a second
headline row. It is optional after v1 and is not a v1 UI panel; the two-cluster test is
the v1 evidence for the heuristic. If both builds are measured later, publish both rows
or neither.

QPS and p95 in the table are means across the three timed passes; the min/max range is
in `bench.json` and the Benchmark tab, not recomputed in the browser.

No number goes in this table without:

- a `run_id` and a `bench.json` row;
- a commit SHA with `dirty=false`;
- dataset / query-set / ground-truth SHA-256;
- CPU, RAM, compiler, flags, thread count;
- three timed passes and their range.

The release report is committed as `docs/results/bench.json`; the raw dataset stays in
gitignored `data/`. Local scratch runs may write `data/bench.json`, but the UI and
README read the release artifact. See `DECISIONS.md` D16. If a number is not in
`bench.json`, it does not go on a slide.

---

## Architecture

```text
React UI
  ├── Documents      upload/paste, list, delete
  ├── Ask            question -> answer + citations
  ├── Map            2D PCA of real embeddings
  └── Benchmark      reads bench.json
        │
        ▼
C++ API
  ├── DocumentStore  SQLite: documents, chunks, embeddings
  ├── OllamaClient   embed + generate
  ├── RagService     retrieve + prompt + citations
  └── HNSW index     in memory, rebuilt on startup
        │
        ▼
vf_engine (static library)
  VectorStore · BruteForce · HNSW · metrics

Separate process, same engine library:

  SIFT-100k -> hns_bench -> bench.json -> Benchmark tab
```

- **SQLite is the source of truth.** The HNSW graph is a derived index, rebuilt from
  live embeddings on startup.
- **The benchmark is a CLI.** It never runs inside an HTTP request; network and JSON
  latency do not contaminate the SIFT numbers.
- **The engine is a library.** The server, benchmark, and tests link the same code.
- **The product is not a microservice.** One C++ server process, one static frontend,
  one SQLite file, one local Ollama server.

The full component map, data model, API, and memory budget are in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## What ships in v1

- A React product with Documents, Ask, Map, and Benchmark tabs.
- Upload/paste `.txt`/`.md`, chunking, Ollama embeddings, SQLite persistence, delete.
- Answers with citations to real retrieved chunks; a grounded "not found" path.
- A 2D map of real embeddings; no fake vectors.
- A from-scratch C++20 HNSW engine: flat store, squared L2, brute force, insert/search/
  prune, directed graph, mark-deleted.
- A SIFT-100k benchmark: 10,000 queries, a small `efSearch` curve, three timed passes
  per point, `bench.json`.
- Unit/invariant tests, a two-cluster prune test, mark-delete and restart tests, and an
  API test with a fake Ollama.
- A README with a short, documented local setup, real numbers, and limitations.

The build order is dependency-driven: trusted data, then the engine, then the product
flow, then the benchmark, then the demo. The definition of done is in
[`docs/START-HERE.md`](docs/START-HERE.md) §5.

---

## Documents

| File | What it is |
|---|---|
| [`docs/START-HERE.md`](docs/START-HERE.md) | the on-ramp: product + engine, scope, dataset ladder, definition of done |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | the locked choices and why (D1–D20) |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | components, data model, API, persistence, memory, tests |
| [`docs/HNSW-SPEC.md`](docs/HNSW-SPEC.md) | the algorithm contract, directed graph, prune worked example, mark-deleted |
| [`docs/plan.md`](docs/plan.md) | the deep product/algorithm/measurement reference |
| [`docs/SHOWCASE.md`](docs/SHOWCASE.md) | the four product tabs, demo script, and non-goals |
| [`docs/results/README.md`](docs/results/README.md) | the frozen `bench.json` release artifact and how it is promoted |

If two docs disagree, `DECISIONS.md` wins. If a number appears in a doc without a
`run_id`, it is illustrative; replace it with a measured value or delete it.

---

## What is deliberately not here

- A wrapper around FAISS, hnswlib, usearch, or another ANN library. The graph is
  written from scratch.
- A claim of novelty. HNSW is a published algorithm; this is an independent
  implementation and evaluation.
- A 1M sweep. 1M is one optional configuration after 100k is solid.
- A KD-tree. It is a poor fit at 128/768 dimensions and would be decoration.
- A 3D map. 2D shows the same truth without the interaction cost.
- A multi-metric benchmark or a cosine-specific index. Product embeddings are
  normalized; L2 ranking equals cosine ranking.
- Auth, multi-user, teams, billing, agents, Graph RAG, or distributed search.
- A static showcase site. The product is the showcase.
- A graph snapshot. SQLite is the source of truth; the graph rebuilds on startup.

The cut list is `DECISIONS.md` D20. It is part of the scope, not a suggestion.

---

## Limitations (v1)

- The engine is single-threaded; the API serializes engine access.
- The graph is rebuilt from SQLite on startup; startup time grows with the corpus.
- Deletion is mark-deleted in memory; physical compaction is deferred.
- The benchmark is 100k SIFT, not 1M; 1M is optional after v1.
- Squared L2 only; product embeddings are normalized so L2 ranking equals cosine.
- The product accepts `.txt`/`.md` and pasted text; PDF is deferred.
- No auth. The demo is for a local or trusted environment, not a public multi-tenant
  service.
- Uploaded text is trusted input; prompt-injection hardening is not part of v1.
- Numbers are local to the machine in `bench.json`; they are not cloud numbers.
- The product is portfolio-grade v1, not production-grade. Say that plainly.

---

## Planned commands

These commands are the target shape; they are not runnable until the code lands.
Prerequisites: a C++20 compiler, CMake ≥ 3.20, Ninja, SQLite3 development headers,
Node 20+, and Ollama.

```bash
# engine + server + benchmark + tests
cmake --preset release
cmake --build --preset release

# product
./build/vectorforge_server --db data/vectorforge.db --port 8080
cd frontend && npm ci && npm run dev

# benchmark (writes the report the UI reads)
./build/hns_bench \
  --base data/sift/sift_base.fvecs \
  --queries data/sift/sift_query.fvecs \
  --groundtruth data/sift/sift100k_groundtruth.ivecs \
  --groundtruth-dist data/sift/sift100k_groundtruth_dist.fvecs \
  --n 100000 --k 10 --M 16 --efConstruction 200 --ef 50 \
  --ef-grid 10,20,50,100,200 \
  --trials 3 --out docs/results/bench.json
```

Ollama must be running locally for the product:

```bash
ollama serve
ollama pull nomic-embed-text
ollama pull llama3.2
```

The embedding and generation model names are configuration, not hardcoded claims. The
README will record the exact models used for the demo.

---

## What I'd do after v1

A graph snapshot with a corruption test; a concurrent read path; PDF extraction; a
same-host baseline against hnswlib/FAISS at matched recall; and quantization for
memory-bound scale. These are stretch items, not v1 claims.

---

## License

TBD.

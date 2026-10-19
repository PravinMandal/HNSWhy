# HNSWhy — Master Plan (Roadmap v1.2, critic-hardened)

> Goal: rebuild a C++20 HNSW vector index from scratch as **HNSWhy**, fully understood line-by-line, FAANG-defensible, with measured recall-vs-speed numbers. Single source of truth. Date: 2026-10-15. Owner: Pravin.
> Rule: **AI may scaffold config (CMake, Docker, CI), never logic.** Hand-write distances, HNSW insert/search/prune, benchmark harness. Every commit explains WHY. Checkable gate: no committed file Pravin cannot whiteboard.
> Timeline: **8–10 weeks, 1 hr/day parallel with PushInbox** (~50hr effective after context-switch tax). **v1 graded truth = SIFT-100k** (1M = stretch only if RSS headroom proven at Wk5). Week-8 = local benchmark + recall–QPS curve (+ hosted demo only if 2hr server spike passes, else README + curl transcript).
> Name thesis: "HNSW + why" — every parameter justified with a number. If you can't say why M=16, it isn't yours.
> Scope cuts (critic-enforced, all removed items → Stretch list §1.4, never silent creep): KDTree optional, save/load stretch, live server conditional, pgvector 10k-or-literature.

## 0. One-line mission

Build a **C++20 HNSW vector search engine (brute-force ground truth, SIFT-measured recall@k, thin REST wrapper if cheap)** that Pravin can whiteboard for 20 minutes — layers on paper, M/ef tradeoffs from own data, HNSW-vs-IVF answer ready.

## 1. Final product definition

### 1.1 What "done" means (v1 locked)
1. Core lib (`hnswhy/`): `BruteForce` + `HNSW` — one `Index` interface: `add(id, vec)`, `search(query, k, efSearch)`, `remove(id)`. (KDTree → Stretch; BruteForce is the ground truth.)
2. Correctness: **R@1 ≥ 0.85 AND R@10 ≥ 0.90** (conjunction — R@10 alone hides broken descent) vs ivecs ground truth on SIFT-100k at chosen ef, n_queries ≥ 1000, mean ± 95% CI, deterministic seeded query subset (ids in CSV). BF cross-check must agree 100% with ivecs on SIFT-10k or FAIL LOUD (loader/metric bug — never average). Distance identity: squared-L2 consistently, ties → smaller id.
3. Benchmark harness (`bench/`): SIFT 10k → 100k loaders (.fvecs/.ivecs), verbatim protocol §6, CSV out: build s, rssBuildPeakMB + rssSteadyMB, QPS, p50/p95 (+min/max), R@1/10/100. M/ef sweep (≥3 ef + ≥2 M) → recall–QPS curve PNG (YOUR data).
4. Second dataset (mandatory by Wk6): one cosine/normalized embedding slice ≥384d, 10k (e.g. MiniLM sample) with BF-cosine ground truth. **SIFT recall measured L2-only; cosine-index SIFT recall is INVALID (CI rejects); cosine recall only on normalized set.** SIFT flatters HNSW — never claim transfer without both rows.
5. Thin REST wrapper (`server/`) ONLY if 2hr Wk4 spike passes, else CLI + curl transcript suffices for v1. Routes if built: `POST /index`, `POST /search`, `GET /health`, `GET /metrics` (≤10k demo).
6. Demo: README table + curve + machine specs (zero X/Y) + 2-min Loom (paper layers in video) + DEMO.md stranger-run. Live URL only if spike passed.
7. Tests green: distance/metric, insert→found, recall gate in CI, delete-then-search (incl. top-layer + 20%-delete recall ≥0.85), dim-mismatch throws, empty/k>n/ef-edges, metric-mismatch 400, clustered kill-test (closest-N impostor check), concurrent contract test or documented single-writer rule.

### 1.2 Non-goals (v1 OUT)
- No RAG/Ollama/LLM. No sharding/distributed. No filtering/namespaces. No Python bindings. No WASM. No auth/billing. Stretch/Phase-2: KDTree, save/load WAL, filtering, batch upsert, Docker image, live server, 1M graded run.

### 1.3 SLO targets (measure; coupled — cherry-picking banned)
- Local (specs row mandatory): p50/p95 reported ONLY at the efSearch achieving R@10 ∈ [0.90, 0.92]; faster points with lower recall do NOT satisfy SLO. Target zone: p50 <2ms / p95 <5ms.
- Hosted small-index (10k, if built): 10–50 QPS, p95 <300ms network-bound.
- Literature calibration only: FAISS ~866 QPS SIFT1M sub-2ms vs Qdrant ~216 / pgvector ~154 (arXiv Aug 2026) — cite, never claim.
- Caps: graded 100k local; 1M iff Wk5 RSS extrapolates inside laptop RAM; hosted 10k.

### 1.4 Stretch / Phase-2 (explicit, not silent)
KDTree (Euclidean-only, d≤10 or PCA-8 slice), save/load snapshot + WAL, filtering/namespaces, batch upsert, live server hardening, SIFT-1M graded, Docker image, Python bindings.
**SOTA-backed stretch notes (sourced, not spec'd for v1):** (a) range-search needs a NATIVE `range_search(radius)` + range-recall metric (|V∩L|/|V| over non-empty ground truth, Def 2.4 arXiv 2305.04359v1) — never "top-k + radius post-filter" (vanilla beam up to ~100× slower: arXiv 2502.13245v3); (b) filtered search = `IDSelector`-style in-filtering during beam expansion (FAISS `SearchParametersHNSW::sel`; UNG SIGMOD'24, Filtered-DiskANN WWW'23, SeRF) — never post-filter-only; (c) delete churn: hnswlib `mark_deleted` sets a DELETE_MARK bit (stolen from L0 size field) and nodes STILL route navigation (nmslib/hnswlib#321); naive delete+reinsert recall drops every 5% cycle — only FreshDiskANN α>1 rules hold >95% 5-recall@5 (arXiv 2105.09613v1); IP-DiskANN 2025 shows replacement costs O(R³) vs O(cR) in-place → Phase-2 churn gate: 50-cycle 5%-churn stability + α-RNG param (default α≈1.2, assert >1) + lazy-DeleteList + `allow_replace_deleted` slot reuse + consolidate pass; (d) quantization-aware stage (int8 SQ search + fp16/full rerank — PyANNS won NeurIPS'23 Big-ANN OOD/streaming; streaming winner corrected after Puck caching-bug PR #280 — benchmark-gaming warning) as recall-vs-QPS Pareto; (e) IVF-PQ memory math for the HNSW-vs-IVF whiteboard: IVFADC = ceil(M·nbits/8)+8 B/vec (M=8,nbits=8 → 16 B/vec incl. id vs 512 B raw SIFT-128f32 = ~32×); HNSW overhead ADDITIVE on full vectors: (d·4 + M·2·4) B/vec (d=128,M=32 → 512+256=768 B/vec ≈ 48× IVF-PQ16; nmslib/HNSW 512+796MB vs IVF16384,Flat 512+8MB on 1M — FAISS wiki) → IVF-PQ wins memory-bound billion-scale, HNSW wins speed/recall small-scale; IVF nprobe saturates (SIFT-1M IVF16384: nprobe 1/4/16/64/256 → R@1 0.4085/0.6331/0.8263/0.9470/0.9861 @ 0.076/0.067/0.078/0.141/0.344ms; nlist ≈ C·√n, demo default IVF4096,Flat) — draw the saturating curve, never claim linear; (f) ann-benchmarks recall is DISTANCE-based (returned items with dist ≤ true-kth-dist + 1e-3, not id-overlap) and QPS = 1/best_search_time over --runs with NO warmup, batch vs single timing distinguished (erikbern/ann-benchmarks metrics.py/runner.py) → our CI uses id-overlap vs ivecs+BF (self-consistent) AND reports threshold-rule recall alongside for comparability; (g) defaults: hnswlib M=16/efC=200 (mult_=1/ln(M), efC clamped ≥M, maxM0=2M — copy getRandomLevel exactly, histogram-test mean) vs FAISS M=32/efC=40/efS=16 with NATIVE METRIC_INNER_PRODUCT (no transform; HNSW has NO remove_ids — "would destroy the graph", faiss#3339) → v1 ships hnswlib-profile defaults + FAISS-compat profile row in sweep; IP comparator = `1-dot` (ordering ≡ max-IP, NO normalization — hnswlib space_ip.h), cosine ONLY via dual normalization + asserts (FAISS wiki: normalize before add AND search; |x−y|²=2−2⟨x,y⟩ exact iff both normalized) + `l2_vs_cosine_equivalence` test; (h) USearch speed = SimSIMD kernels + stored dtypes f32/f16/bf16/i8/b1 (default cos, connectivity=16, expansion 128/64; uint40_t neighbor lists, keyed add/search/remove/rename, multi-vec keys) → template distance kernels on dtype with SIMD dispatch, f16→f32 / i8→i32 widened accumulation (never native-width; NumKong successor lib), 64-bit external keys from day one; (i) FlatNav 2024: H hierarchy gives ~zero benefit at d≥32 (flat NSW matches recall, ~38% less build memory) → document as the honest counter-argument in the whiteboard answer; (j) fvecs/ivecs = headerless LE, per-record [int32 dim][dim×payload] (4+d·4 B fvecs-f32 / ivecs-i32; 516-B base/query records vs 404-B GT rows with k=100 leading int, squared-L2 ordered — TEXMEX/corpus-texmex + faiss demo_sift1M.cpp).

## 2. Tech stack (locked Wk1)

| Layer | Choice | Why |
|---|---|---|
| Core | C++20, CMake, `-O2 -pthread`, Linux-first (g++ ≥ 10 / clang++ ≥ 11 — std::span is C++20; C++17 cannot compile it) | Amalgamated `hnswhy.h+cpp` ONLY until Wk1 exit. Split trigger: >800 lines OR 2nd index lands. Fixed boundary: `distance.*` / `store.*` (owner §3.1) / `brute_force.*` / `hnsw.*` / `fvecs.*` (bench-only, never server-linked). Headers self-contained (`#pragma once`, explicit includes, no `using namespace`). No raw `new/delete` anywhere (CI greps). |
| Store | **Flat**: `vector<float> data(N*dim); vector<int> ids;` move-only insert (`const float*` or `&&`). NO `vector<vector<float>>` (fragmented 1M killer). | Decided, not debated |
| Tests | GoogleTest via FetchContent (90-min timebox; else plain-assert `tests/main.cpp` — recall test still mandatory; Wk1 fails if neither green) | THE test = recall-vs-BF |
| Datasets | SIFT10k/100k (.fvecs/.ivecs) + 10k cosine embedding slice | Ground truth ships with data |
| Baseline | pgvector SIFT-10k ONLY, 4hr hard stop, load script written BEFORE clock starts; else row = LITERATURE + resume drops claim | Timeboxed |
| API | **Default: SKIP server in v1 if Project #3 can call CLI/binary.** Else ONE thin wrapper (Crow vs FastAPI-shim, 2hr timeboxed spike, no new framework after Wk4 Day 2) | Demo + gateway consumer |
| Plots | matplotlib → PNG in README | The curve is the interview |
| CI | GHA: build + tests + recall-gate (R@1≥0.85 AND R@10≥0.90, SIFT-10k, ≥1000 queries) + clustered kill-test | No silent rot |

## 3. Architecture (mental map)

```
VECTORS (strict fvecs reader: int32 dim LE + dim×float32 LE per record, memcpy-out never reinterpret_cast,
         ifstream gcount per record, LE assert via known SIFT dim 128, ivecs int32 ground truth, corrupt → fail fast)
  │ dim validated once + EVERY insert (mismatch → throw, never OOB)
  ▼
INDEX (one interface, two impls v1)
  ├─ BruteForce: score-all, nth_element top-k (ground truth; cross-check must match ivecs 100% on 10k)
  └─ HNSW: L0=all/sparse-uppers/global-IDs; level=min(sampled, maxLevel=ceil(log_M(Nmax))+2);
       Rng object injected (seeded MT19937 repro / random_device default — ALL randomness flows through it, seed in every CSV row);
       insert: randLevel(clamped u∈[1e-9,1), mL=1/log(M) internal non-constructible, assert 4≤M≤64)
         → ef=1 descent (skip layers with <2 nodes; N<efBuild → efBuild_eff=N)
         → per-layer beam(efBuild) → Alg.4 Select-Neighbors-Heuristic VERBATIM (pinned pseudocode in docs/prune.md; ctor flags `extendCandidates=true, keepPrunedConnections=true` + Wk5 2×2 ablation CSV columns; candidate pool = beam(efBuild) [+neighbors-of-W iff extend, capped 2×efBuild]; budgets M (L>0) / Mmax0 (L0) enforced BOTH sides incl. back-edge re-prune)
         → bidir link + neighbor re-prune on overflow (degree ≤ M/M0 enforced both sides)
         → levelCount[l]++, promote entry
       search: beam(efDescent=1) at L>0 (sweep 1 vs 4 on clustered synthetic Wk5; topLayerEnterCount metric) → beam max(efSearch,k) at L0 (clamp ef_eff=max(efSearch,k)≤N; N==0→{}; k>N→all sorted; efSearch==0→400); entryPt rule pinned (newNode iff newLevel>topLayer else unchanged; on entryPt delete → max-layer live node, smallest id tie-break; test delete-entry-100× asserts layer(entryPt)==topLayer)
         → top-k. Visited = timestamp array (vector<int> tag + curGen, touched-list clear, ZERO heap allocs hot path; heaps reserved).
       remove: erase adjacencies + levelCount-- + while(topLayer>0 && levelCount[topLayer]==0) topLayer-- + per-level representative entryPt;
         empty → tombstone (topLayer=0, entryPt=nullopt, search→{}, save/load round-trips). DELETE frees id; reinsert = fresh sample (test: new vec found, BF parity, degree≤M0).
       Concurrency contract (v1): std::shared_mutex — search shared_lock, mut exclusive_lock (OR single-threaded + server serializesWrites → 409 on write-during-search; pinned in Wk1, never assumed).
  ▼
BENCH (verbatim §6 protocol) → CSV → curve PNG (x-axis includes R@1 point)
```

Metric pinned per index at construction; cross-metric query → 400. RNG: single injected object always.

### 3.1 Ownership (normative, no exceptions)
1. Single owner: `VectorStore` owns ALL vectors as flat `vector<float> data_ (N*dim)` + `vector<ExternalId> ids_` + `unordered_map<ExternalId,Slot>` with `using ExternalId=int64_t; using Slot=uint32_t;` (overflow check before `slots_.size()==UINT32_MAX` → throw; CI boundary test). Store 64B-aligned (`aligned_alloc(64,…)`) for AVX. Indexes own NO vector data — only slot ids + adjacency as **CSR per layer** (`vector<Slot> neigh; vector<uint32_t> off; neigh.reserve(N*M0*1.1)` — `vector<vector<Slot>>` BANNED, it re-fragments what flat store fixed). `Node` holding `vector<float>` is BANNED by review.
2. `add(ExternalId, vector<float> vec)` by value, `std::move` into store — one copy at boundary, zero after. Never store `const&`. **Strong guarantee**: `reserve` everything first, then mutate adjacency via RAII scope-guard rollback (erase back-edges, pop store tail, erase map, decrement levelCount); throwing-allocator test asserts state parity.
3. `search(const float* q, size_t dim, size_t k, size_t ef) const [[nodiscard]]` (C++20: `span<const float>` overload allowed) — query pointer valid for call only, never stored.
4. No raw `new/delete` (CI grep). No `shared_ptr<vector<float>>`. Vectors `const` after insert. Moves `noexcept`; destructors `noexcept`; `search/size/dim/distances` are `const [[nodiscard]]`; `-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow` on all jobs.
5. Every index class move-only: delete copy/assign, default move, `static_assert(!is_copyable_v<HNSW>)` in a test.

### 3.2 Thread-safety contract (v1: coarse single-writer, readers concurrent, no upgrading)
1. One `mutable shared_mutex mu_` per index. `search` → `shared_lock`; `add/remove/load/save` → `unique_lock` (save NEVER shared — fuzzy+realloc = corrupt snapshot; quiesce or exclusive). No lock upgrading (shared→exclusive without release = deadlock, banned by review). Reader-preferring rwlock can starve writers: 4-thread bench includes 1-writer+3-readers 10s; writer p99 wait >1s ⇒ fall back to documented single-threaded+serialize-writes path.
2. **Visited state is thread-local** (shared `tag/curGen` under `shared_lock` is a data race — UB, TSan fires). `thread_local vector<int32_t> tag (grown on add) + thread_local uint32_t gen` with wrap guard (`if(++gen==0){fill(tag,0);gen=1;}`). Touched-list clear per query. Zero heap allocs hot path: thread_local scratch binary heaps (`cand` reserve efMax*2, `result` reserve efMax, efMax=400; hand-rolled index arithmetic, no `std::priority_queue`; `.clear()` reuse, never shrink). Allocation test (10k queries post-warmup, zero mallocs) in CI.
3. Server never hands out spans past lock scope — `search` copies top-k `Result{id,distance}` under lock, returns by value.
4. Bench reports single-threaded QPS (graded) + 4-thread QPS with TSan clean (`-DSANITIZE=thread` CI job on server path; race = fail).
5. Non-goals logged: no per-node locks, no lock-free beam, no atomic adjacency. Coarse lock first; shard only after measured contention.

## 4. HNSW rules (do not violate)

1. Params = constructor args (`M, M0=2M, efBuild, mL internal`). Nothing hardcoded. README states values + sweep-justification.
2. Level: clamped sample + `maxLevel` cap + `mL=1/log(M)` internal; `N<efBuild→eff=N`; tiny-N descent skip; test `rng→0.0 ⇒ cap, no throw`; test `N=5,M=16 ⇒ recall 1.0 exact`.
3. Prune = Alg.4 verbatim (pinned pseudocode in `docs/prune.md`); closest-N BANNED; neighbor re-prune on back-edge overflow; kill-test: 2-cluster synthetic (closest-N R@10<0.5, heuristic>0.9) in CI.
4. Remove = histogram repair O(maxLevel) + tombstone + reuse semantics + tests (sole-top delete, reinsert-new-vec, 20%-delete recall≥0.85).
5. No `operator[]` on hot paths (`.at()/find`).
6. KDTree (stretch only): Euclidean-only + 400-on-cosine + parity on d≤10/PCA-8 only; Rule-of-Five safe; document-degraded (not parity-gated) on raw SIFT-128.
7. Dim validation throws FIRST on every add/search/load (never `assert` on user input/file/alloc — asserts are for unreachable internal invariants only, e.g. `assert(layer<=topLayer)` post-repair). Core throws `DimMismatch/MetricMismatch/DuplicateId/PersistenceError : invalid_argument` (+ untouched `bad_alloc`); server maps to 400/409/500 (never traces); bench pre-`reserve`s and catches `bad_alloc` → prints `NEED ~X BYTES…exit 2`. Release (`-DNDEBUG`) dim-mismatch test proves no assert-dependence.
8. **Persistence layout v1 (Stretch; frozen when built — LE, no raw-struct dumps):** `[magic 'HNSY'][version u32=1][metric u8][dim u32][count u64][mLevelMult f64][M u32][efBuild u32]` then per slot `[id u64][dim×f32 LE]` then per layer `[layerCount u32][neighborCount u32][neighborSlot u32…]` then `[crc32]`. `static_assert` LE writer. Load rejects bad-magic/version-mismatch/dim-metric-mismatch/crc-fail with `PersistenceError` (never partial-load). `fwrite(&struct)`/`reinterpret_cast` dumps BANNED. Round-trip + corrupt-one-byte tests.
8. Persistence → Stretch (v1 fallback = rebuild-from-fvecs, documented). No silent 3× copies: flat store §2.
9. Real parsers only (fvecs memcpy-spec §3; JSON lib if server built — never substring/stoi-prefix/silent-drop).
10. Wording: "teaching-grade HNSW with measured recall" — never production-grade / O(log N) unmeasured / semantic without embeddings.

## 5. API contract (ONLY if server spike passes)

- `POST /index {id?, vector[dim], metric}` → 201 / 400 DIM_MISMATCH / METRIC_MISMATCH / 409 DUP_ID.
- `POST /index/batch` → per-item statuses (never silent partial).
- `POST /search {query[dim], k 1–100 def 10, efSearch?}` → 200 {results, tookUs (full-handler µs integer; <1µs → "<1"), efSearch} / 400s. Bench never reports per-query us — only aggregated p50/p95 over ≥1000.
- `GET /health` {ok,count,dims,metric,M,efBuild,topLayer} / `GET /metrics` {…from CSV, never invented} / `DELETE /index/:id` → 204 (repair rule 4).

## 6. Benchmark spec (verbatim protocol — missing field ⇒ CSV rejected)

```
threads=1 (taskset -c 0), governor=performance + cpupower excerpt + turbo/c-states/THP recorded,
flags="-O2 -pthread" + g++ --version verbatim (+ one -O3 -march=native A/B row or drop flag claim),
allocator recorded (ldd --version + MALLOC_CONF, MALLOC_ARENA_MAX=1), machine row 12 fields
(cpu_model, cores/threads, smt, numa, kernel, microcode, governor+turbo, c-states, compiler, flags,
allocator, THP, loadavg + turbostat throttle check), warmup 20 discarded, latency = median over ≥5 trials
(fresh process each; single-trial = PRELIMINARY, never gated),
QUERIES: full SIFT query set graded (comparability with literature); CI = first 1000 deterministic
(queryRange=0:1000 in CSV) — random subsampling FORBIDDEN in graded runs. Pinned subset sha256 in README;
seed change = new row, never overwrite (anti seed-shopping). Difficulty proxy: BF top-1/top-2 ratio deciles.
n=1000 ⇒ Wilson half-width ±0.019 at p=0.9 — thresholds <0.02 apart UNRESOLVABLE, never claim [0.90,0.92] windows.
CI: Wilson score 95% on binary hit vectors (NOT normal approx); GATE = Wilson LOWER bound ≥ threshold
(R@10 primary ≥0.90; R@1 ≥0.85 diagnostic floor point-estimate; Bonferroni if joint-claimed; Wk5 bootstrap-BCa
validates Wilson once, |diff|>0.005 ⇒ bootstrap wins).
GROUND TRUTH: ivecs-primary AND BF cross-check BOTH reported (R@10_ivecs, R@10_BF); BF-vs-ivecs must agree
≥99.5% with ALL mismatches ties-within-eps=1e-6 else FAIL; CI gates on BF variant (self-consistent), ivecs informational.
Distance: squared-L2 consistently, ties→smaller id; README annotates sqrt-omitted + one sqrt-on/off p95 A/B row.
SIFT metric = L2 ONLY (cosine-index SIFT recall INVALID, CI rejects). Cosine only on normalized set with BF-cosine GT.
report QPS + p50/p95 + min/max + R@1/10/100 triple + build_s + build_dist_comps + inserts_per_s
+ rssPeakChildMB + rssSteadyChildMB (fork() child: loads/builds/queries/prints ru_maxrss + VmHWM post-free+trim then _exit;
gtest and bench binaries SEPARATE; loader staging freed BEFORE steady sample).
BUILD SLO: SIFT-100k ≤600s single-thread; SIFT-1M ≤7200s or mandatory efBuild cut; t_1M ≈ t_100k×10×1.3 gate at Wk5.
SLO row = lowest-latency config with Wilson lower ≥0.90 (off-target curve points greyed, never claimed; full Pareto
frontier over M×efSearch plotted, dominated hollow, CI whiskers both axes, grid pre-registered, post-hoc grid = new version).
pgvector baseline (§6.1): version pinned (digest), index SQL verbatim (≥3 swept points, NOT defaults), local docker SAME host,
same 1000 query ids, server-side EXPLAIN ANALYZE median AND client median SEPARATE; max parallel workers=1;
any field missing ⇒ LITERATURE-ONLY, no ratio printed.
CSV schema v1 (bench/check_csv.py validates; CI fails on mismatch):
dataset,n_vectors,dim,metric,M,efBuild,efSearch,phase(A-build/B-query),ext,keep,n_queries,query_seed,query_ids_sha256,
threads,pin,governor,turbo,compiler,flags,build_s,build_dist_comps,inserts_per_s,rssPeakChildMB,rssSteadyChildMB,
thpMode,arenaMax,qps,p50_ms,p95_ms,pmin_ms,pmax_ms,r1,r1_lo,r1_hi,r10,r10_lo,r10_hi,r100,hubTop5Share,r10p10,
git_commit,timestamp_utc,schema_v. Query ids sidecar queries-<sha8>.txt. schema_v bump on ANY change.
CI (per-PR): SIFT-10k pinned, gate lower-bound ≥0.90. Nightly (scheduled): SIFT-100k + cosine slice, NO gate, trend CSV + curve diff.
Parameter change needs BOTH green; 10k-only tuning regressing nightly-100k beyond Wilson half-width REVERTS.
Latency SLO counts only inside gated recall; tookUs = full-handler integer µs (<1µs → "<1"); bench aggregates only.
Contention row (mandatory Wk5+): 4 threads taskset -c 0-3, 95/5 search/add Poisson @80% maxQPS, p50/p95/p99 + writer-wait-ms;
p99>3×p50 = lock-convoy FAIL. perf row (10k pilot): cache-misses, DTLB-load-misses; >2% DTLB ⇒ MADV_HUGEPAGE + value in CSV.
```

## 7. VectorForge flaws → HNSWhy fixes

V1 KD+cosine→§4.6+400 test. V2 topLayer→histogram+tombstone+tests. V3 closest-N→Alg.4+killer-test. V4 metric→pinned+400. V5 OOB→asserts+validation. V6 baked params→constructor+Rng-injection. V7 3×copies/no-persist→flat store (+save/load stretch). V8 hand-JSON→real parsers. V9 theater→§6+gate. V10 no-recall→triple+CI. V11 keyword-stub→DELETED (embeddings validated-path only). V12 provenance→out of scope, consumers cite. V13 silent-cutoff→explicit reason+param. V14 windows-rot→Linux-first+gitignore+CI. V15 zero-tests/dumps→day-one tests+scoped commits. V16 ollama→N/A + per-insert dims + per-item batch status. V17 ghosts/overprov→rule 5 + sweep-justified values.
Algorithmic critic: U=0 guard, level cap, tiny-N rules, mL internal, prune pseudocode + re-prune, O(maxLevel) repair, id-reuse semantic, shared_mutex contract, timestamp-visited + zero-alloc, ef/k edges, delete-heavy test, ivecs-primary + R@1 gate + 1000 queries/CI, cosine second dataset + L2-only rule, KD scope fix, 7-control bench block, dual-RSS, SLO coupling, tookUs rule, Rng injection.

## 8. Learning contract (7-gate — replaces old 5)

Gate (aloud, no notes, before Wk3 code):
1. KD vs HNSW in 128/768-dim WITH curse math (branching vs distance-computation dominance) — slogans fail.
2. efSearch cost/benefit + why efSearch ≥ k (recall hinge).
3. Layers drawing from memory (L0-all, sparse-uppers, global IDs, greedy local minima).
4. M → memory, efBuild → build time, efSearch → query time (mapping with own numbers by Wk6).
5. recall@k methodology: ivecs-primary + BF-agree-100%-or-fail + ties-by-smaller-id + why 1000 queries (±CI).
6. **HNSW vs IVF-PQ vs brute-force — pick with N/memory/latency numbers** (why not FAISS-IVF? memory-bound 1B → PQ; small N → BF).
7. **Memory math for 100k/1M at chosen M + what breaks first at 10× scale.**
Teach-steps → `docs/learn-<topic>.md`: curse math, greedy+beam, Alg.4 prune, recall@k+CI, fvecs bytes, IVF-PQ landscape (for Q6).
Wk8 whiteboard script MUST include 2-min HNSW-vs-IVF answer with own curve as evidence, else not done.

## 9. Build order (8–10 weeks, 1hr/day)

### 9.0 Spike (3 days, NON-throwaway skeleton survives to Wk1)
CMake lib+bench+tests skeleton + FetchContent gtest (or assert-harness — timer visible) + flat-store decision + strict 100-vec fvecs reader (dim-throw) + chrono microbench of BF-1k-queries showing nonzero p50. Criterion met ⇒ Wk1 starts; else spike extends, plan slips openly (never silently).

- Wk1: distances + BruteForce + tests + CI build green. Exit: `ctest` green + storage pinned.
- Wk2: HNSW insert/search/remove (params day-one, Alg.4 prune, histogram repair, tombstone, Rng injection, shared_mutex contract) + unit tests. Exit: 1k random-vector recall-vs-BF self-test + N=5 exact test + zero-rng test.
- Wk3: SIFT-10k loader + recall harness (warmup/trials/p50/p95/CI-seeded 1000 queries) + clustered kill-test. Exit: first REAL CSV + R@1/R@10 gate evaluated (may fail openly — values tuned Wk5).
- Wk4: server decision spike (2hr timer; default SKIP unless Project #3 needs HTTP) + `/health` if built. Exit: curl/CLI round-trip on 10k or documented SKIP.
- Wk5: SIFT-100k + cosine embedding slice (≥384d, BF-cosine GT) + M/ef sweep (≥3 ef, ≥2 M) + RSS accounting + 1M-go/no-go (RSS extrapolation). Exit: curve PNG draft + both dataset rows.
- Wk6: pgvector SIFT-10k baseline (4hr hard stop, script-first; else LITERATURE row) + README table (zero X/Y) + machine row. Exit: table complete.
- Wk7: demo polish (400s, edges, DEMO.md) + Loom (paper layers) + repo hygiene audit. Hosted 10k only if Wk4 spike passed.
- Wk8: resume bullet (measured) + 7-gate whiteboard script (incl. IVF answer) + gateway handshake (Project #3 → /search or CLI).
- Stretch: KDTree, save/load, filtering, 1M graded, Docker, bindings.

## 10. Tests (green before demo)

Unit: metrics (+zero-vector convention), dim-throws, insert→found, remove→topLayer/entryPt/tombstone/reinsert-new-vec, save→load (stretch), empty/k>n/ef-edges (ef> N, k>N→all-sorted, ef=0→400, N=0→{}), metric-400, U=0-cap, N=5-exact.
Recall: SIFT-10k gate (R@1+R@10, 1000 seeded queries, CI), BF-agree-100% check, 2-cluster kill-test, 20%-delete recall≥0.85, cosine-set BF-cosine parity.
API (if built): 400s, batch partial-ids, delete-repair-search, health/metrics shapes.
Regression: seeded repro CSV + random_device default path.

## 11. Infra & deploy (₹0)

- Local (graded truth): CMake + g++ + gtest; `data/` gitignored + download script; `bench/` CSV + `plots/` PNG; taskset/pinned protocol §6.
- Live (conditional): 10k index Render-or-shim (Wk4 decides); pgvector Supabase free; DB-free health only if probed.
- Hygiene: `.gitignore` (build/, binaries, compile_commands.json, data/*.fvecs, PDFs); never `db.exe`; scoped commits (`feat(hnsw):`, `test(recall):`); every commit builds + tests green; bench CSV rows carry seed+flags+specs.

## 12. Resume bullet (fill after §6 CSV — coupled values only)

- `Built C++20 HNSW vector index (brute-force ground truth) — R@1 X / R@10 0.9Y at p95 Z ms on SIFT-100k (local, <CPU/RAM/OS/flags>); cosine embedding slice row; recall–QPS curve + pgvector row in README.`

## 13. Risks & open questions

- R1: 1M RSS > laptop → decided at Wk5 extrapolation; 100k graded stands regardless.
- R2: gtest fights → assert-harness (Wk1 spike, timer visible).
- R3: C++ HTTP pain → SKIP server (CLI suffices) — default-leaning.
- R4: pgvector >4hr → LITERATURE row, resume drops claim (pre-agreed, no shame).
- OQ1: server shape (Wk4 2hr). OQ2: snapshot format (stretch — custom binary, layout doc'd when built).

## 14. Definition of done

- [ ] SIFT-100k R@1+R@10 + p50/p95 + QPS + build + dual-RSS in README (zero X/Y) + curve PNG (R@1 point) + machine specs + seed/flags
- [ ] Cosine-slice row (BF-cosine GT) — transfer claimed only with both rows
- [ ] Tests green incl. CI gate + kill-test + delete-heavy + edges (U=0, N=5, tombstone, reuse)
- [ ] Loom (paper layers) + DEMO.md stranger-run (+ live 10k only if spike passed)
- [ ] Zero hardcoded M/ef/seed/metric; zero keyword stubs; zero unmeasured adjectives
- [ ] Clean git (incremental, scoped, no binaries/dirty tree)
- [ ] 7-gate whiteboard incl. 2-min HNSW-vs-IVF with own curve, no notes

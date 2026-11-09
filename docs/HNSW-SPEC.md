# VectorForge / HNSWhy — HNSW Engine Specification

> The hardest part of the project is not the benchmark; it is getting the graph
> invariants and the product boundary right. This file is the contract for the engine
> library. Read it before writing the graph, not on day one. Pseudocode here is
> notation, not implementation: the C++ is yours to write.
>
> Canonical decisions: `DECISIONS.md` D5–D10. Architecture: `ARCHITECTURE.md` §5.
> Prune worked example: §7.2 below.

---

## 1. Scope

The engine is an in-memory HNSW library used by two consumers:

- the VectorForge server, for live document retrieval;
- the `hns_bench` CLI, for the SIFT-100k measurement.

v1 implements:

- a single-threaded HNSW graph over a flat vector store;
- insert, search, and prune;
- multiple layers with greedy descent;
- deterministic level assignment;
- **directed adjacency** (no symmetry invariant);
- **mark-deleted** nodes for product deletion;
- a `validate()` method for tests.

v1 does **not** implement:

- physical deletion or graph compaction;
- graph persistence or load;
- concurrent search/insert;
- filtering, range search, or quantization;
- a trace/replay API in the engine.

Persistence belongs to the product layer: SQLite stores the embeddings, and the graph
is rebuilt on startup. The benchmark builds the graph once per run. The engine has no
`ifstream`, no SQL, no JSON, and no `main()`.

---

## 2. Notation, types, and parameters

| Symbol | Meaning | v1 default |
|---|---|---:|
| `N` | number of stored slots | 100,000 graded |
| `dim` | vector dimension | 128 for SIFT; 768 for product embeddings |
| `M` | max neighbours per node above layer 0 | 16 |
| `M0` | max neighbours at layer 0 | `2 * M` = 32 |
| `efConstruction` | beam width during insert | 200 |
| `efSearch` | beam width during query | 50 for product; swept in benchmark |
| `k` | results returned | 10 for R@10; 1 diagnostic |
| `topLayer` | highest layer currently populated | computed |
| `entry` | global entry node | node at `topLayer` |
| `level(v)` | highest layer containing node `v` | sampled |
| `levelCount[l]` | number of nodes with `level >= l` | computed |
| `mL` | level decay constant | `1 / ln(M)` |
| `maxLevel` | hard cap on sampled level (must be ≤ 255) | 32 |

Core types:

```cpp
using Slot = uint32_t;

enum class PruneStrategy { Heuristic, NearestOnly };

struct HNSWParams {
    size_t M = 16;
    size_t M0 = 32;
    size_t efConstruction = 200;
    size_t maxLevel = 32;
    uint64_t seed = 42;
    PruneStrategy prune = PruneStrategy::Heuristic;
};

struct Candidate {
    float dist;
    Slot id;
};

struct SearchStats {
    uint64_t distanceComputations = 0;
    uint32_t hops = 0;
    uint32_t layerVisits = 0;
};

struct SearchResult {
    std::vector<Slot> ids;
    std::vector<float> distances;
    SearchStats stats;
};
```

`M0` defaults to `2*M` at the CLI/config layer. If both are set, require `M0 >= M`;
the struct defaults above are the v1 point (`M=16`, `M0=32`), not a rule that `M0`
stays 32 when `M` changes.

**Distance.** Squared L2 for v1:

```
d(a, b) = sum_i (a[i] - b[i])^2
```

No square root: it is monotonic and cheaper. The metric is fixed per index. The v1
data contract excludes NaN/Inf; the hot path does not guard them. Zero vectors are
valid for distance, but the product rejects zero-norm embeddings before normalization;
do not divide by zero.

**IDs.** v1 IDs are slot indices (`uint32_t`) into the vector store. If external IDs
are ever needed, add a separate `external_id` array; do not put a map in the hot path.
The product maps slots to chunk IDs outside the engine.

**Tie-breaking.** Every comparison uses `(distance, id)`, never distance alone. This
makes results deterministic when two vectors are equidistant.

---

## 3. Graph model and invariants

### 3.1 Directed adjacency

The graph is **directed**. When a new node is connected, edges are added in both
directions as a starting point, but pruning is one-sided. If `u` lists `v`, `v` is not
required to list `u`. There is no reciprocal-cleanup pass.

This is the standard HNSW shape in production implementations. Enforcing symmetry is
an extra constraint on every prune, can remove useful one-way links, and adds a
cleanup path that is easy to get wrong. The property that matters is search quality and
reachability, not a symmetric matrix.

### 3.2 Layer membership

- Layer 0 contains every slot, including tombstones.
- A node with `level = L` appears in layers `0, 1, ..., L`.
- An edge at layer `l` connects two nodes that both have `level >= l`.

### 3.3 Degree caps

- Layer 0: `degree(v) <= M0`.
- Layer `l > 0`: `degree(v) <= M`.

### 3.4 Required invariants

`validate()` must check all of these after every insert/remove in small tests:

1. No self-loop: `v` is not in `neighbours(v, l)`.
2. No duplicate neighbour: each ID appears once per layer.
3. Degree caps hold.
4. Layer membership: every neighbour at layer `l` has `level >= l`.
5. Entry point: `level(entry) == topLayer`, and `entry` is a valid slot.
6. `levelCount[l]` equals the number of nodes with `level >= l`.
7. No edge to a node with a lower level than the layer.
8. If `N > 0`, `topLayer >= 0`; if `N == 0`, `entry` is unset.
9. Layer counts decrease with height: `levelCount[0] = N`, `levelCount[l] <=
   levelCount[l-1]`.
10. Deleted nodes may remain in the graph; they are not returned by search.

There is **no symmetry invariant**. The old "if `u` lists `v`, `v` lists `u`" rule is
removed.

The test suite also checks **reachability** from the entry point at layer 0 on small
graphs. Reachability is a diagnostic, not an HNSW invariant; a directed graph can be
valid and still have unreachable nodes. If reachability on `sift10k` is poor, that is
a quality bug to investigate, not a `validate()` failure.

### 3.5 Ownership

`HNSW` owns only graph state:

- `const VectorStore& store_`;
- `std::vector<uint32_t> links0_` (`N * M0`);
- `std::vector<uint16_t> degree0_`;
- upper-layer adjacency (sparse);
- `std::vector<uint8_t> level_`;
- `std::vector<uint32_t> levelCount_`;
- `std::vector<uint8_t> deleted_`;
- `uint32_t entry_`, `int topLayer_`;
- RNG state/seed.

It never copies the vectors. During a build or search, the store is not mutated. On
insert, the product adds the vector first, then calls `HNSW::insert(slot)`; the slot ID
is stable even if the store's float buffer grows. Do not hold a `std::span` across
`VectorStore::add`.

---

## 4. Level assignment

Use a seeded `std::mt19937_64` (or a documented equivalent). For each new node:

```
u = (rng() + 1) / (max_uint64 + 1.0)     // u in (0, 1]
level = floor(-ln(u) / ln(M))            // mL = 1/ln(M)
level = min(level, maxLevel)
```

- `M >= 4` is required; assert/handle at construction.
- `M0 <= 65535` because `degree0_` is `uint16_t`; the v1 defaults are far below.
- `u` must be strictly positive. If `u == 0`, resample or clamp; document the choice.
- `maxLevel` is a hard cap, default 32; it must not depend on the current `N` because
  the graph grows after construction. At `M=16`, the expected maximum level for 1M
  nodes is about 5, so a cap of 32 does not truncate the distribution in practice.
- `maxLevel <= 255`, because `level_` is `uint8_t`; reject a larger value at
  construction.
- The same seed must produce the same levels on the same toolchain. If cross-toolchain
  reproducibility is claimed, implement the float conversion explicitly; standard
  library distributions are not guaranteed identical.

The expected distribution is geometric: about `1/M` of nodes reach layer 1, `1/M^2`
reach layer 2, and so on. A histogram test checks the shape, not exact counts.

---

## 5. Insert

### 5.1 Preconditions

- The store contains the new vector at slot `id`; `get(id)` remains valid for the
  insert.
- `dim` matches.
- `id` is not already present.
- The HNSW arrays have capacity for `id`; if not, grow them geometrically before
  touching the new slot. Slot IDs do not change on growth.
- `id` is not marked deleted.
- "Graph is empty" means `slot_count() == 0`, not `size() == 0`. An all-tombstone graph
  is not empty; insertion must attach through the tombstones (§5.3).

### 5.2 Procedure

```
insert(id):
  l = sampleLevel()
  level[id] = l

  if graph is empty:
      entry = id
      topLayer = l
      for layer in 0..l: levelCount[layer]++
      return

  ep = entry
  oldTop = topLayer

  // 1. Greedy descent through layers above the new node's level.
  for layer = oldTop down to l+1:
      ep = greedySearchLayer(q=vec(id), ep, layer)

  // 2. Connect from min(l, oldTop) down to 0.
  for layer = min(l, oldTop) down to 0:
      candidates = searchLayer(q=vec(id), ep, layer, ef=efConstruction,
                               liveOnly=false)
      neighbours = selectNeighbours(id, candidates, cap(layer))
      connect(id, neighbours, layer)
      if degree(id, layer) > cap(layer):
          prune(id, layer)
      for each newly connected neighbour u:
          if degree(u, layer) > cap(layer):
              prune(u, layer)          // back-edge repair, reference = u
      ep = closest candidate (the beam's best node)

  // 3. Only now may the new node become the entry point.
  for layer in 0..l: levelCount[layer]++
  if l > oldTop:
      entry = id
      topLayer = l
```

**Ordering is not optional.** Connect first, then promote `entry`/`topLayer`.
Promoting first makes search start from a node with no edges.

### 5.3 Neighbour selection on insert

`selectNeighbours(newId, candidates, cap)` uses the same diversity heuristic as
pruning (§7), with the reference point `newId`. Candidates are the beam results from
`searchLayer` with `efConstruction`, using the all-node variant (`liveOnly=false`):
tombstones are eligible candidates, so a new node can attach even when every live
document was deleted. Query uses the live-only variant (§6.4). This is deliberate:
deletion changes what queries return, not graph connectivity. New nodes may spend
degree on tombstones; high churn is a rebuild signal.

Optional flags (implement only if measured):

- `extendCandidates`: add the neighbours of the candidates to the pool before
  selection. This can improve diversity; cap the pool at `2 * efConstruction`.
- `keepPrunedConnections`: after the heuristic, fill remaining capacity with the
  closest rejected candidates. This keeps degrees full; it can reduce diversity.

The default is **heuristic only, no fill**. `keepPrunedConnections` is a flag; measure
it on `sift10k` before changing the default. The two-cluster test must pass either way.

For the back-edge repair of an overflowing neighbour `v`, **do not** use
`extendCandidates`; use `v` as the reference point. This is the classic bug: using
the new node as the reference for `v`'s prune produces the wrong ordering.

### 5.4 Connect and back-edge repair

`connect(newId, neighbours, layer)` adds both directions:

```
for each v in neighbours:
    add newId -> v at layer
    add v -> newId at layer
```

Then the caller repairs each neighbour `v` whose list overflowed:

```
prune(v, layer):
  pool = neighbours(v, layer)
  selected = selectNeighbours(v, pool, cap(layer))   // reference = v
  replace neighbours(v, layer) with selected
  // Do NOT remove v from rejected neighbours' lists.
  // The graph is directed; a one-way edge is allowed.
```

The last comment is the difference from the old symmetric spec. If `r` is rejected
from `v`'s list, `r` may still list `v`. That is fine. Do not add a reciprocal-cleanup
pass.

If `u`'s degree falls below cap, that is fine; do not recursively re-add. Recursive
repair is a stretch item, not v1.

---

## 6. Search

### 6.1 Preconditions

- `k >= 1`; `efSearch >= 1`.
- `ef_eff = min(max(efSearch, k), slot_count())`.
- If `slot_count() == 0`, return empty.
- If `k > live_count`, return all live results sorted.
- Deleted slots are traversed but not returned.

### 6.2 Procedure

```
search(q, k, efSearch):
  if slot_count() == 0: return {}
  ef = min(max(efSearch, k), slot_count())

  ep = entry
  for layer = topLayer down to 1:
      ep = greedySearchLayer(q, ep, layer)

  results = searchLayer(q, ep, layer=0, ef)   // live results only
  return top k of results sorted by (distance, id)
```

If every node is deleted, return empty. The entry point may be deleted; it still
routes.

### 6.3 `greedySearchLayer`

```
greedySearchLayer(q, ep, layer):
  current = ep
  currentDist = d(q, vec(current))
  changed = true
  while changed:
      changed = false
      for each neighbour v of current at layer:
          dv = d(q, vec(v))
          if (dv, v) < (currentDist, current):
              current = v; currentDist = dv; changed = true
  return current
```

At upper layers, `ef=1` is intentional: the job is to reach a good entry point for
the next layer, not to explore broadly. Deleted nodes are allowed as routing nodes.

### 6.4 `searchLayer` (beam)

Use two heaps:

- **candidates**: min-heap by `(distance, id)` — the closest unexplored node.
- **results**: max-heap by `(distance, id)` — the worst of the best `ef` so far.

```
searchLayer(q, ep, layer, ef):
  visited = per-layer marker
  mark ep visited
  push ep into candidates with d(q, ep)
  if not deleted(ep):
      push ep into results with d(q, ep)

  while candidates not empty:
      c = pop closest candidate
      if results.size >= ef and
         (c.dist, c.id) > (results.worst.dist, results.worst.id):
          break

      for each neighbour v of c at layer:
          if visited(v): continue
          mark visited(v)
          dv = d(q, vec(v))

          if results.size < ef or
             (dv, v) < (results.worst.dist, results.worst.id):
              push v into candidates
              if not deleted(v):
                  push v into results
                  if results.size > ef:
                      pop worst from results

  return results
```

Re-read `results.worst()` after every push/pop; do not cache the worst distance across
neighbour iterations. If the result set is not full, always consider the neighbour.

**Per-layer visited.** A global visited tag shared across layers can skip a node at
layer 0 because it was seen at layer 2, losing lower-layer expansion. Use a per-layer
generation:

```
visitedTag[node] == visitedGen[layer]
```

When entering a layer, increment `visitedGen[layer]` (with wrap handling). This is
cheap and correct. Alternatively, keep a separate visited list per layer, but do not
assume a global tag is equivalent without a test.

**Termination.** Stop when the closest candidate is worse than the worst result and
the result set is full. An empty candidate heap also terminates. Do not stop merely
because the best distance did not change for one iteration; that is a heuristic that
can hurt recall.

**Result size.** The result heap capacity must be at least `ef_eff`; the final top-k
is a subset. `ef` must be at least `k`.

**Deleted filtering.** Deleted nodes may stay in the candidate heap and are expanded,
but they are **not inserted into the result heap**. For queries, the result heap holds
live nodes only, so `ef` is a budget for live results, not tombstone slots. Do not filter
deleted nodes out of traversal: that can disconnect the graph and hide live neighbours
behind a tombstone. If fewer than `k` live nodes are found, return what exists; do not
pad the result.

**Two variants.** `searchLayer` takes a `liveOnly` flag. `search()` passes `true`; the
pseudocode above is that variant. `insert()` passes `false`, so the `if not deleted`
guards are omitted and tombstones enter the result heap. Without that variant, inserting
into an all-tombstone graph would produce an isolated node that queries cannot reach.
Query stays live-only so `ef` is not spent on tombstone results.

---

## 7. Prune (Algorithm 4)

`selectNeighbours(ref, candidates, cap)` chooses a diverse subset of candidates for
the neighbour list of `ref`.

### 7.1 The predicate

For each candidate `c`, in increasing order of `d(c, ref)`:

- Keep `c` if, for every already-selected neighbour `s`,
  `d(c, s) > d(c, ref)`.
- Reject `c` otherwise (it is closer to an existing selected neighbour than to
  `ref`, so it is redundant for connectivity).
- Stop when `cap` neighbours are selected.

Ties in `d(c, ref)` are broken by ID. The comparison is strict: if
`d(c, s) == d(c, ref)`, reject to avoid redundancy.

### 7.2 Worked example

Reference node `ref = (0, 0)`, `M = 2`. All distances below are **squared** L2.

| ID | Point | `d²(ref, ·)` | `d²(A, ·)` | `d²(D, ·)` |
|---|---|---:|---:|---:|
| A | (-0.1, 0) | 0.01 | 0 | — |
| B | (-0.2, 0) | 0.04 | 0.01 | — |
| C | (-0.3, 0) | 0.09 | 0.04 | — |
| D | (0.3, 0) | 0.09 | 0.16 | 0 |
| E | (1.0, 0) | 1.00 | 1.21 | 0.49 |
| F | (1.0, 1.0) | 2.00 | 2.21 | 1.49 |

Sorted by `(d²(ref, ·), ID)`: A (0.01), B (0.04), C (0.09), D (0.09), E (1.00),
F (2.00).

**Nearest-only, M=2:** keep **A, B**. Both are on the left; the right side has no
edge from this node.

**Heuristic, M=2:**

1. Select **A**.
2. B: `d²(B, A) = 0.01 <= d²(B, ref) = 0.04` → reject (same direction, redundant).
3. C: `d²(C, A) = 0.04 <= d²(C, ref) = 0.09` → reject.
4. D: `d²(D, A) = 0.16 > d²(D, ref) = 0.09` → **select D**. The right side is now
   reachable.
5. Capacity is full; stop.

This is the whole heuristic in one table: **A** is the closest, **B** and **C** are
redundant with A, and **D** is the opposite-direction node. Nearest-only drops D; the
heuristic keeps it. The two-cluster test is the real bridge test; this 2D example is
the smallest picture of the same predicate.

The two-cluster test uses this geometry at a larger scale (two Gaussian clusters, a
boundary node, `M = 8`). The test threshold is:

- nearest-only: cluster-1 reachability from the entry `< 0.5` (or cluster-1 recall
  far below the heuristic), and
- heuristic: cluster-1 reachability `> 0.9`.

Those thresholds are gates, not illustrative headline numbers. The test output is the
artifact; do not hardcode "41% → 99%" in the README unless you can point to the test
run.

### 7.3 Fill-to-M

If `keepPrunedConnections` is enabled, after the heuristic fill the remaining slots
with the closest rejected candidates. This keeps degrees full but can reduce
diversity. Implement it as a flag, measure it on the 10k set, and pick a default based
on the curve. The two-cluster test must still pass.

### 7.4 Back-edge repair

Use `ref = v` (the node whose list overflowed), candidates = `v`'s current
neighbours. Do **not** use the newly inserted node as the reference for `v`'s list.
The graph is directed, so rejected neighbours do not need their reciprocal edge
removed.

---

## 8. Mark-deleted

v1 **does** support deletion, as mark-deleted. This is required by the product: a user
can delete a document.

Rules:

- Keep the vector and all edges; the node still routes.
- `deleted[id] = true`.
- `remove` is idempotent (a second call is a no-op) or asserts that the slot is live;
  pick one and test it. Do not decrement `size()` twice.
- Query search traverses deleted nodes but never returns them.
- Insertion treats tombstones as ordinary candidates (`liveOnly=false`), so a new node
  can attach after a delete-all. Queries still exclude them.
- `size()` counts live nodes; `slot_count()` counts all slots including tombstones.
- Reinsertion uses a new slot; do not reuse a tombstone in v1.
- The entry point may be deleted and still routes; if all nodes are deleted, search
  returns empty.
- On restart, the product rebuilds from the SQLite rows; deleted documents are gone, so
  tombstones disappear.
- `validate()` still passes with tombstones.
- Tests: deleted IDs never returned; live neighbours still found; reinsert new vector;
  delete-all returns empty; delete-all then insert one is found; restart after delete.

Mark-deleted is safe for routing but not free. A high tombstone ratio consumes traversal
work and can crowd the candidate frontier, reducing recall until the index is rebuilt.
v1 does not promise recall under heavy in-memory deletion; the product rebuilds from
SQLite rows on restart, and physical graph compaction is deferred. Test a modest delete
ratio, not a 50% churn benchmark.

Physical deletion with reciprocal cleanup, slot reuse, and connectivity repair is a
stretch item. Do not put it in the v1 critical path.

---

## 9. Persistence boundary

The engine has no `save()` or `load()` in v1.

- **Product:** SQLite stores documents, chunks, and embeddings. On startup, the
  server loads the rows, builds the `VectorStore`, and calls `HNSW::build()`. On
  insert, SQLite commits first, then the graph inserts the new slots. On delete,
  SQLite deletes the document row (cascade), then the graph marks the slots.
- **Benchmark:** the CLI builds the graph once per run and exits. It does not use
  SQLite.
- **Why:** a graph snapshot is a second source of truth with its own format version,
  corruption, and consistency bugs. Rebuilding is simpler and cannot drift. The cost
  is startup time, measured separately.

A graph snapshot is a stretch item with a corruption test and a versioned format. It
is not v1.

---

## 10. Determinism and ties

- Seed the level RNG explicitly; store the seed in every `bench.json` row.
- Insert order is deterministic (slot order) unless a test deliberately shuffles it.
  Document that graph quality depends on insertion order; do not silently shuffle in
  the benchmark.
- Every heap comparator is `(distance, id)`.
- Results are sorted by `(distance, id)`.
- `std::priority_queue` is acceptable if you do not need to inspect/update the beam;
  a reserved vector with `std::push_heap`/`pop_heap` is easier to defend. Pick one
  and test it.
- Do not claim cross-toolchain reproducibility of the level distribution unless you
  implement the float conversion yourself.

---

## 11. Validation and tests

`HNSW::validate()` checks the invariants in §3.4. Run it:

- after every insert in a small random test (`N <= 200`);
- after every prune in the two-cluster test;
- after a full 10k build, at least once (it is O(N * M) and can be slow at 100k; use
  a sampled version or run it in the 10k CI job);
- after a mark-delete on a small graph.

Test seams:

| Seam | What it proves |
|---|---|
| `levelDistribution` | geometric decay, no infinite loop, cap works |
| `greedySearchLayer` on a hand-built line | reaches the closest node |
| `searchLayer` on a hand-built graph | matches exhaustive top-k for `N <= 20` |
| `insert` 0/1/2 nodes | entry/topLayer/levelCount ordering |
| `insert` random 200 | `validate()` green after every insert |
| `prune` worked example | A,D selected; B,C rejected |
| two-cluster fixture | nearest-only fails, heuristic passes |
| `search` edges | empty, k=1, k>N, ef=1, ef>N, duplicate distances, ties |
| mark-delete | deleted IDs never returned; live neighbours still found; entry safe |
| delete-all then insert | new live node attaches via tombstones and is returned |
| full 10k | report R@10 at the chosen config; no second floor |
| full 100k | R@10 ≥ 0.90 point on `sift100k` (release) |

The two-cluster test is the permanent guard for the prune heuristic. If someone
"simplifies" `selectNeighbours` to nearest-only, this test must fail.

---

## 12. Complexity and memory

Let `N` be the number of slots, `M` the degree cap, `efC` the construction beam, and
`efS` the search beam.

- **Insert:** expected `O(M log N + efC * M)` distance/edge work. The upper-layer
  descent is `O(M log N)` with `ef=1`; the beam runs on the expected `O(1)` layers the
  node joins (the geometric level distribution gives `M / (M - 1)`), each at
  `O(efC * M)`. Build is `N` inserts and is dominated by `N * efC * M` for the
  default parameters.
- **Search:** expected `O(M log N + efS * M)`: `O(M log N)` for the greedy descent and
  `O(efS * M)` for the layer-0 beam.
- **Memory:** layer 0 links `N * M0 * 4` bytes; degree `N * 2`; upper layers roughly
  `N * M * 4 / (M - 1)` bytes (geometric sum); levels `N`; deleted flags `N`; visited
  tags `N * 4` per search scratch. At 100k / M=16 / M0=32: ~12.8 MB of L0 links,
  ~0.2 MB degree, ~0.4 MB upper links.

HNSW's advantage is fewer distance computations, not free memory. The interview answer
should state both: `avg_distance_computations` vs `N`, and bytes/vector vs brute force.

---

## 13. Product integration contract

The product layer uses the engine through this contract:

1. **Slots are stable for the process lifetime.** `VectorStore::add` returns a slot;
   the product keeps it in the in-memory `slot_to_chunk_` map. SQLite stores chunk
   IDs, not engine slots. On restart, slots are reassigned from the rows and the
   graph is rebuilt.
2. **Normalize embeddings before insertion and query.** Ollama embeddings are
   normalized to unit length, so squared L2 ranking equals cosine ranking. The engine
   does not normalize automatically; the product boundary does.
3. **One mutex around engine access.** The engine is not thread-safe. The API
   serializes insert, delete, and search.
4. **SQLite commits first.** The graph is a derived index. If the process dies between
   the commit and the graph insert, startup rebuild repairs it.
5. **Deleted slots are filtered by the engine.** The product should not receive
   tombstone IDs from `search()`. It still maps live slots to chunks.
6. **No SQL or HTTP in the engine.** The engine takes `std::span<const float>` and
   returns slots. The product owns text, prompts, citations, and errors.

---

## 14. Common bugs and interview questions

1. **CSR for an incremental graph.** You cannot insert into CSR without shifting or
   rebuilding. Use fixed-capacity adjacency at layer 0.
2. **Global visited across layers.** It skips nodes at layer 0 that were seen at a
   higher layer. Use per-layer generations.
3. **Wrong reference point in back-edge repair.** Prune `u`'s list using distances
   to `u`, not to the new node.
4. **Promoting the entry before connecting.** Search starts from an isolated node.
5. **`levelCount[l]` counts nodes exactly at `l`.** It must count nodes with
   `level >= l`, or `topLayer` repair is wrong.
6. **Distance-only comparator.** Non-deterministic results on ties.
7. **`ef < k`.** The result heap cannot hold k answers. Use `ef_eff = max(ef, k)`.
8. **Terminating when the best does not improve for one step.** You will under-search
   the beam. Use the candidate-vs-worst-result rule.
9. **Physical delete of a bridge node.** The graph can split. Mark-deleted is the v1
   answer.
10. **Filtering deleted nodes out of traversal.** The graph can disconnect. Traverse
    them, filter only the result.
11. **Assuming symmetry.** The graph is directed; a reciprocal-cleanup pass is not
    part of this design. Validate degrees and recall, not symmetry.
12. **Stale slot mapping after restart.** Slots are reassigned when the graph is
    rebuilt; map them from the current rows, never from an old cached table.
13. **Normalization mismatch.** If documents are normalized and the query is not (or
    vice versa), cosine ranking is wrong even though the engine is correct.
14. **Assuming mark-deleted is free at high delete ratios.** Tombstones can fill the
    beam. Rebuild from SQLite or compact; do not claim 50% churn works because a
    small test passed.
15. **Inserting after a delete-all with a live-only candidate beam.** The new node has
    no live candidates and becomes unreachable. Insert uses the all-node beam
    (`liveOnly=false`); query remains live-only. Also check `slot_count() == 0`, not
    `size() == 0`, for the empty-graph branch; an all-tombstone graph is not empty.
16. **Claiming O(log N) without measuring.** Say "sublinear in practice; measured
    distance computations" and show the numbers.

The 20-minute whiteboard should be able to draw: layers, one insert, one search, the
prune predicate, the two-cluster failure, and the SQLite-to-graph rebuild. If you can
do that, the code is yours.

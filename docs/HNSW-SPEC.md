# HNSWhy — HNSW Specification

> The hardest part of the project is not the benchmark; it is getting the graph
> invariants right. This file is the contract. Read it before Session 7, not on day
> one. Pseudocode here is notation, not implementation: the C++ is yours to write.
>
> Canonical decisions: `DECISIONS.md` D4–D9. Architecture: `ARCHITECTURE.md` §5.
> Prune worked example: §7.1 below.

---

## 1. Scope

v1 implements:

- a single-threaded, in-memory HNSW graph over a flat vector store;
- insert, search, and prune;
- multiple layers with greedy descent;
- deterministic level assignment;
- a `validate()` method for tests;
- a compile-time trace hook for the showcase.

v1 does **not** implement physical deletion, persistence, concurrency, filtering,
range search, or quantization. Those are stretch items in `plan.md` §12.

---

## 2. Notation and parameters

| Symbol | Meaning | v1 default |
|---|---|---:|
| `N` | number of stored vectors | 100,000 graded |
| `dim` | vector dimension | 128 |
| `M` | max neighbours per node above layer 0 | 16 |
| `M0` | max neighbours at layer 0 | `2 * M` = 32 |
| `efConstruction` | beam width during insert | 200 |
| `efSearch` | beam width during query | swept, e.g. 10–200 |
| `k` | results returned | 10 (R@10), 1/100 diagnostics |
| `topLayer` | highest layer currently populated | computed |
| `entry` | global entry node | node at `topLayer` |
| `level(v)` | highest layer containing node `v` | sampled |
| `levelCount[l]` | number of nodes with `level >= l` | computed |
| `mL` | level decay constant | `1 / ln(M)` |
| `maxLevel` | hard cap on sampled level | `ceil(log_M(N)) + 2`, at least 4 |

**Distance.** Squared L2 for v1:

```
d(a, b) = sum_i (a[i] - b[i])^2
```

No square root: it is monotonic and cheaper. The metric is fixed per index.

**IDs.** v1 IDs are slot indices (`uint32_t`) into the vector store. If external IDs
are ever needed, add a separate `external_id` array; do not put a map in the hot
path.

**Tie-breaking.** Every comparison uses `(distance, id)`, never distance alone.
This makes results deterministic when two vectors are equidistant.

---

## 3. Graph model and invariants

The v1 graph is **symmetric**: if `u` lists `v`, then `v` lists `u`. Pruning a
neighbour removes the reciprocal edge. This is the simplest model to validate and
to explain. It is not the only valid HNSW design, but it is the one this project
uses.

### 3.1 Layer membership

- Layer 0 contains every node.
- A node with `level = L` appears in layers `0, 1, ..., L`.
- An edge at layer `l` connects two nodes that both have `level >= l`.

### 3.2 Degree caps

- Layer 0: `degree(v) <= M0`.
- Layer `l > 0`: `degree(v) <= M`.

### 3.3 Required invariants

`validate()` must check all of these after every insert/remove in small tests:

1. No self-loop: `v` is not in `neighbours(v, l)`.
2. No duplicate neighbour: each ID appears once per layer.
3. Degree caps hold.
4. Layer membership: every neighbour at layer `l` has `level >= l`.
5. Symmetry: `v` is in `u`'s list iff `u` is in `v`'s list, at the same layer.
6. Entry point: `level(entry) == topLayer`, and `entry` is a valid node.
7. `levelCount[l]` equals the number of nodes with `level >= l`.
8. No edge to a node with a lower level than the layer.
9. If `N > 0`, `topLayer >= 0`; if `N == 0`, `entry` is unset.
10. Layer counts decrease with height: `levelCount[0] = N`, `levelCount[l] <= levelCount[l-1]`.

The test suite also checks **reachability** from the entry point at layer 0 on small
graphs. Reachability is a diagnostic, not an HNSW invariant; a graph can be valid
and still have poor recall.

### 3.4 Ownership

`HNSW` owns only graph state:

- `const VectorStore& store_`;
- `std::vector<uint32_t> links0_` (`N * M0`);
- `std::vector<uint16_t> degree0_`;
- upper-layer adjacency (sparse);
- `std::vector<uint8_t> level_`;
- `std::vector<uint32_t> levelCount_`;
- `uint32_t entry_`, `int topLayer_`;
- RNG state/seed.

It never copies the vectors. The store is immutable during a build/search; reserve
`N` before building so slots do not move.

---

## 4. Level assignment

Use a seeded `std::mt19937_64` (or a documented equivalent). For each new node:

```
u = (rng() + 1) / (max_uint64 + 1.0)     // u in (0, 1]
level = floor(-ln(u) / ln(M))            // mL = 1/ln(M)
level = min(level, maxLevel)
```

- `M >= 4` is required; assert/handle at construction.
- `u` must be strictly positive. If `u == 0`, either resample or clamp; document.
- `maxLevel = ceil(log_M(max(N, 2))) + 2`, at least 4.
- The same seed must produce the same levels on the same toolchain. If cross-toolchain
  reproducibility is claimed, implement the float conversion explicitly; standard
  library distributions are not guaranteed identical.

The expected distribution is geometric: about `1/M` of nodes reach layer 1, `1/M^2`
reach layer 2, and so on. A histogram test checks the shape, not exact counts.

---

## 5. Insert

### 5.1 Preconditions

- The store contains the new vector at slot `id` and will not reallocate.
- `dim` matches.
- `id` is not already present.
- `HNSW` capacity was reserved for `N` nodes.

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
      ep = greedySearchLayer(q=vec(id), ep, layer, ef=1)

  // 2. Connect from min(l, oldTop) down to 0.
  for layer = min(l, oldTop) down to 0:
      candidates = searchLayer(q=vec(id), ep, layer, ef=efConstruction)
      neighbours = selectNeighbours(id, candidates, M(layer))
      addBidirectionalEdges(id, neighbours, layer)
      for each neighbour u:
          if degree(u, layer) > cap(layer):
              prune(u, layer)          // back-edge repair, reference = u
      ep = closest candidate (for the next lower layer)

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
`searchLayer` with `efConstruction`.

Optional flags (implement both, measure both):

- `extendCandidates`: add the neighbours of the candidates to the pool before
  selection. This can improve diversity; cap the pool at `2 * efConstruction`.
- `keepPrunedConnections`: after the heuristic, fill remaining capacity with the
  closest rejected candidates. This keeps degrees full; it can reduce diversity.

For the back-edge repair of an overflowing neighbour `u`, **do not** use
`extendCandidates`; use `u` as the reference point. This is the classic bug: using
the new node as the reference for `u`'s prune produces the wrong ordering.

### 5.4 Back-edge repair

When `addBidirectionalEdges` makes `u` exceed its cap:

```
prune(u, layer):
  pool = neighbours(u, layer) ∪ {newId}
  selected = selectNeighbours(u, pool, cap(layer))   // reference = u
  replace neighbours(u, layer) with selected
  for each removed neighbour r:
      remove u from neighbours(r, layer)             // keep symmetry
```

If `r`'s degree falls, that is fine; do not recursively re-add. Recursive repair is
a stretch item, not v1.

---

## 6. Search

### 6.1 Preconditions

- `k >= 1`; `efSearch >= 1`.
- `ef_eff = min(max(efSearch, k), N)`.
- If `N == 0`, return empty.
- If `k > N`, return all `N` sorted.

### 6.2 Procedure

```
search(q, k, efSearch):
  if N == 0: return {}
  ef = min(max(efSearch, k), N)

  ep = entry
  for layer = topLayer down to 1:
      ep = greedySearchLayer(q, ep, layer, ef=1)

  candidates = searchLayer(q, ep, layer=0, ef)
  return top k of candidates sorted by (distance, id)
```

### 6.3 `greedySearchLayer`

```
greedySearchLayer(q, ep, layer, ef=1):
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
the next layer, not to explore broadly.

### 6.4 `searchLayer` (beam)

Use two heaps:

- **candidates**: min-heap by `(distance, id)` — the closest unexplored node.
- **results**: max-heap by `(distance, id)` — the worst of the best `ef` so far.

```
searchLayer(q, ep, layer, ef):
  visited = per-layer marker
  mark ep visited
  push ep into candidates and results with d(q, ep)

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
              push v into candidates and results
              if results.size > ef:
                  pop worst from results

  return results
```

Re-read `results.worst()` after every push/pop; do not cache the worst distance across
neighbour iterations. If the result set is not full, always consider the neighbour.

**Per-layer visited.** A global visited tag shared across layers can skip a node at
layer 0 because it was seen at layer 2, losing lower-layer expansion. Use a
per-layer generation:

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
redundant with A, and **D** is the bridge that keeps the other cluster connected.
Nearest-only drops D; the heuristic keeps it.

The two-cluster test in S11–S12 uses this geometry at a larger scale (two Gaussian
clusters, a boundary node, `M = 8`). The test threshold is:

- nearest-only: cluster-1 reachability from the entry `< 0.5` (or cluster-1 recall
  far below the heuristic), and
- heuristic: cluster-1 reachability `> 0.9`.

Those thresholds are gates, not illustrative headline numbers. The measured values
go in the CSV/showcase; do not hardcode "41% → 99%" in the README.

### 7.3 Fill-to-M

If `keepPrunedConnections` is enabled, after the heuristic fill the remaining slots
with the closest rejected candidates. This keeps degrees full but can reduce
diversity. Implement it as a flag, measure it on the 10k set, and pick a default
based on the curve. The two-cluster test must still pass.

### 7.4 Back-edge repair

Use `ref = u` (the node whose list overflowed), candidates = `u`'s current
neighbours plus the new node. Do **not** use the newly inserted node as the reference
for `u`'s list. Remove reciprocal edges for every rejected neighbour.

---

## 8. Delete policy

v1 has **no physical delete**. If deletion is added later, use mark-deleted:

- keep the vector and all edges; the node still routes;
- `deleted[id] = true`; search filters deleted IDs from the final result but may
  traverse them;
- `size()` counts live nodes; `slot_count()` counts all slots including tombstones;
- reinsert uses a new slot; do not reuse a tombstone in v1;
- the entry point may be deleted and still routes; if all nodes are deleted, search
  returns empty;
- tests: deleted IDs never returned; live neighbours still found; reinsert new
  vector; delete-all returns empty; `validate()` still passes.

Physical deletion with reciprocal cleanup, slot reuse, and connectivity repair is a
stretch item. Do not put it in the v1 critical path.

---

## 9. Determinism and ties

- Seed the level RNG explicitly; store the seed in every CSV/JSON row.
- Insert order is deterministic (slot order) unless a test deliberately shuffles it.
  Document that graph quality depends on insertion order; do not silently shuffle in
  the benchmark.
- Every heap comparator is `(distance, id)`.
- Results are sorted by `(distance, id)`.
- `std::priority_heap` with `std::push_heap`/`pop_heap` is acceptable if reserved; a
  hand-rolled heap is a later optimization. Do not use `std::priority_queue` if you
  need to inspect/update the beam; a reserved vector is easier to defend.
- Do not claim cross-toolchain reproducibility of the level distribution unless you
  implement the float conversion yourself.

---

## 10. Validation and tests

`HNSW::validate()` checks the invariants in §3.3. Run it:

- after every insert in a small random test (`N <= 200`);
- after every prune in the two-cluster test;
- after a full 10k build, at least once (it is O(N * M) and can be slow at 100k; use
  a sampled version or run it in the 10k CI job).

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
| `searchTraced` | IDs/distances identical to `search` for 100 queries |
| full 10k | R@10 ≥ 0.95 point on sift10k |
| full 100k | R@10 lower bound ≥ 0.90 on sift100k (release) |

---

## 11. Complexity and memory

Let `N` be the number of nodes, `M` the degree cap, `efC` the construction beam, and
`efS` the search beam.

- **Insert:** roughly `O(efC * M * log N)` distance/edge work per layer, times the
  number of layers the node reaches. Build time is dominated by
  `N * efC * M`.
- **Search:** roughly `O(efS * M * log N)`.
- **Memory:** layer 0 links `N * M0 * 4` bytes; degree `N * 2`; upper layers roughly
  `N * M * 4 / (M - 1)` bytes (geometric sum); levels `N`; visited tags `N * 4` per
  search scratch. At 100k / M=16 / M0=32: ~12.8 MB of L0 links, ~0.2 MB degree,
  ~0.8 MB upper links.

HNSW's advantage is fewer distance computations, not free memory. The interview
answer should state both: `avg_distance_computations` vs `N`, and bytes/vector vs
brute force.

---

## 12. Common bugs and interview questions

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
10. **Claiming O(log N) without measuring.** Say "sublinear in practice; measured
    distance computations" and show the numbers.

The 20-minute whiteboard should be able to draw: layers, one insert, one search, the
prune predicate, and the two-cluster failure. If you can do that, the code is yours.

# SESSIONS — HNSWhy

One row per session (2–3 hours). Fill it in at the end of the session, before you
close the laptop. This is the honest answer to "am I on track." Feeling is not data.

`LOC by me` = lines **you** wrote, not lines the agent wrote. Be strict; inflating
this defeats the purpose of the log.

The canonical plan is `WORKFLOW.md` §5. Do not renumber rows. If a session slips,
mark it `partial` and carry the checkpoint forward; do not silently skip it.

| # | date | hours | LOC by me | what works now | what is broken | checkpoint (evidence) |
|---|---|---|---|---|---|---|
| 1 | 2026-10-22 | — | 0 | CMake presets; `hns_core`/`hns_io`/`hns_bench`/`hns_tests`; `--version` prints Release + flags; 3 smoke tests green | No distance, loader, search, or benchmark yet (by design) | `cmake --build --preset release`; `ctest --preset release`; `./build/release/hns_bench --version` |
| 2 | 2026-10-22 | — | 0 | Squared L2 + 5 hand-checked/edge tests; synthetic k=1 scan prints p50/p95 | No SIFT loader, graph, or top-k BF yet (by design) | `ctest --preset release -R SquaredL2` (5 pass); `./build/release/hns_bench --synthetic-bench` (p50/p95 nonzero and stable across 3 trials) |
| 3 | | | | | | `siftsmall` loads 10,000 × 128; self-distance 0 |
| 4 | | | | | | `siftsmall` BF vs shipped GT: tie-aware R@10 = 1.0000 |
| 5 | | | | | | `sift100k` BF GT fixture (IDs + distances + query IDs + sha256); self-contained `sift10k` CI bundle; BF vs own GT = 1.0000; float64 sample agrees |
| 6 | | | | | | `sift10k` BF vs own GT = 1.0000; CI build green; query hash recorded |
| 7 | | | | | | `validate()` on hand-built graph: no self/duplicate; degree caps |
| 8 | | | | | | level histogram ~1/M; entry at top; `levelCount` correct |
| 9 | | | | | | beam search matches exhaustive for N ≤ 20; per-layer visited works |
| 10 | | | | | | 10k NSW R@10 ≥ 0.85 at ef=100; degree ≤ 32 |
| 11 | | | | | | nearest-only two-cluster: cluster-1 reachability < 0.5 (or recall far below heuristic) |
| 12 | | | | | | heuristic two-cluster: cluster-1 reachability > 0.9; nearest vs heuristic recorded; `validate()` green |
| 13 | | | | | | 10k HNSW R@10 ≥ 0.90; upper-layer gain measured or documented |
| 14 | | | | | | 10k R@10 ≥ 0.95 point; ef sweep; graph code frozen (params still sweep) |
| 15 | | | | | | `searchTraced` == `search` (IDs + distances) for 100 queries; no trace in timed path |
| 16 | | | | | | 100k first run: R@10 ≥ 0.90 point; 10k queries or labelled preliminary |
| 17 | | | | | | edge cases green; `validate()` on 10k; memory measured; graph code freeze |
| 18 | | | | | | 5-trial CSV: build once; warmup 100; p50/p95/p99; QPS; RSS; distance comps; CI recall gate green |
| 19 | | | | | | BF baseline row: same kernel/flags/thread; speedup at SLO cached |
| 20 | | | | | | 6–8 measured points from M × efC × ef sweep; no blank cells |
| 21 | | | | | | full Pareto + CIs; pre-registered SLO; no band; no interpolation |
| 22 | | | | | | **v1 core ship:** 100k / 10k queries / 3 build seeds × 5 trials; artifacts schema-valid; README no placeholders; `dirty=false` |
| 23 | | | | | | exporter writes `manifest.json`, `bench.json`, `queries/q0042.json`, `prune.json`; schema check green |
| 24 | | | | | | `/` deployed; every hero number maps to `run_id`; reproduce footer |
| 25 | | | | | | `/trace`: graph + replay; 500-node cap; results vs GT; keyboard controls |
| 26 | | | | | | `/prune`: reachability and recall labelled separately; README image; numbers match CSV |
| 27 | | | | | | 20-minute whiteboard, no notes; 60-second demo; Loom; `DECISIONS.md` complete |
| 28 | | | | | | final tag; optional 1M row labelled separately; no new scope |

**Milestones:** S6 = trusted ruler. S14 = HNSW works on 10k. S16 = first 100k run.
S22 = v1 core ship. S26 = showcase. S28 = final ship/buffer.

---

## Weekly review

Every 4 sessions, answer these three in one line each:

1. Sessions completed / sessions planned:
2. Is the LOC-per-session number rising, flat, or falling?
3. Which checkpoint is furthest behind, and why?

If the answer to (3) is "recall" and you are changing `M`/`ef`, stop. It is a bug,
not a tuning problem. Go back to the last green checkpoint.

---

## Stop conditions

If any of these happen, stop and get help rather than grinding:

- No runnable binary by the end of S3 → the toolchain is fighting you.
- S4 fails and you are editing the loader without a byte-level diff → print the first
  record's dim and floats; compare with `xxd`.
- Recall below 0.90 at S13 and you are twiddling parameters → it is a bug.
- More than 2 sessions on tooling → cut something.
- You cannot explain a committed function in one sentence → revert it.
- The benchmark is rebuilding the index per trial → fix the harness before sweeping.
- A number appears in the README without a `run_id` → delete it.

---

## Notes

Use the space below for decisions that are not in `DECISIONS.md` yet. When a note
becomes a real choice, add it to `DECISIONS.md` and link it here.

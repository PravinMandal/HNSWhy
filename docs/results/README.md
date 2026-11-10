# Benchmark Results

`bench.json` is the frozen release artifact produced by `hns_bench`. The README
table and the product's Benchmark tab read this file. The browser formats the
stored metrics and derives ratios such as speedup from the reported QPS values;
it never re-measures recall or latency.

## Run

```bash
scripts/fetch_sift.sh     # once, if data/sift is missing
scripts/run_bench.sh      # builds release, computes ground truth, runs the sweep
```

The script writes `docs/results/bench.json`. Raw SIFT files and the generated
100k ground truth stay in gitignored `data/`. The committed report was produced
from commit `5ea6228` with `worktree_dirty: false`; provenance also records the
input hashes, run id, compiler flags, and hardware.

## What was measured

- Dataset: SIFT-100k (`sift_base.fvecs` prefix), 128 dimensions.
- Queries: all 10,000 in `sift_query.fvecs`.
- Ground truth: exact top-100 for the 100k prefix, recomputed with
  `hns_gt`; the shipped SIFT-1M answer key does not apply to a prefix.
- Graph: `M=16`, `M0=32`, `efConstruction=200`, seed 42.
- Sweep: `efSearch` 10, 20, 50, 100, 200.
- Three timed passes per point (30,000 measured queries per point) after 200
  warmup queries. Latency percentiles use all measured queries.
- Exact baseline: `BruteForce` over the same 100k base and all 10,000 queries.
  It matched the ground-truth distances within `1e-4`.
- Build: `Release` (`-O3`) GCC 16.2.1, 12th Gen Intel Core i5-12450H,
  Linux 7.2.11. The report records build type, compiler, CPU, OS, and dataset
  paths; `-O3` comes from the Release preset.

## Results (`efSearch = 50`)

| Metric | Value |
|---|---:|
| Recall@1 | 0.9927 |
| Recall@10 | 0.9836 |
| QPS | 7,281 |
| p50 | 0.142 ms |
| p95 | 0.173 ms |
| p99 | 0.184 ms |
| Build time | 33.4 s |
| Peak RSS | 93.1 MB |
| Exact QPS | 200.9 |
| Speedup at this recall | ~36.2x |

Peak RSS is whole-process RSS: it includes the 51.2 MB base prefix, 5.2 MB of
queries, the 8 MB ground-truth arrays, and the graph. It is not an isolated
index-size measurement.

## Full curve

| efSearch | R@1 | R@10 | QPS | p50 ms | p95 ms | p99 ms |
|---:|---:|---:|---:|---:|---:|---:|
| 10 | 0.8779 | 0.7989 | 22,026 | 0.045 | 0.063 | 0.072 |
| 20 | 0.9558 | 0.9123 | 14,387 | 0.070 | 0.090 | 0.102 |
| 50 | 0.9927 | 0.9836 | 7,281 | 0.142 | 0.173 | 0.184 |
| 100 | 0.9985 | 0.9966 | 4,177 | 0.247 | 0.305 | 0.328 |
| 200 | 0.9998 | 0.9996 | 2,435 | 0.424 | 0.530 | 0.565 |
| exact | 1.0000 | 1.0000 | 200.9 | 4.911 | 5.245 | 5.854 |

These are local measurements, not cloud numbers. They must not be copied into
marketing material for other hardware or another dataset. When the code or
benchmark changes, rerun and replace the file; do not edit values by hand.

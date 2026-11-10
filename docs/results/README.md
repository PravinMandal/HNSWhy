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
100k ground truth stay in gitignored `data/`.

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
| Recall@1 | 0.9926 |
| Recall@10 | 0.9839 |
| QPS | 6,895 |
| p50 | 0.149 ms |
| p95 | 0.184 ms |
| p99 | 0.206 ms |
| Build time | 36.1 s |
| Peak RSS | 92.5 MB |
| Exact QPS | 193.5 |
| Speedup at this recall | ~35.6x |

Peak RSS is whole-process RSS: it includes the 51.2 MB base prefix, 5.2 MB of
queries, the 8 MB ground-truth arrays, and the graph. It is not an isolated
index-size measurement.

## Full curve

| efSearch | R@1 | R@10 | QPS | p50 ms | p95 ms | p99 ms |
|---:|---:|---:|---:|---:|---:|---:|
| 10 | 0.8747 | 0.7998 | 21,274 | 0.046 | 0.065 | 0.076 |
| 20 | 0.9562 | 0.9126 | 13,721 | 0.073 | 0.096 | 0.109 |
| 50 | 0.9926 | 0.9839 | 6,895 | 0.149 | 0.184 | 0.206 |
| 100 | 0.9986 | 0.9967 | 3,986 | 0.258 | 0.322 | 0.355 |
| 200 | 0.9998 | 0.9996 | 2,283 | 0.450 | 0.577 | 0.646 |
| exact | 1.0000 | 1.0000 | 193.5 | 5.049 | 5.682 | 6.521 |

These are local measurements, not cloud numbers. They must not be copied into
marketing material for other hardware or another dataset. When the code or
benchmark changes, rerun and replace the file; do not edit values by hand.

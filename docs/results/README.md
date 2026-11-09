# Benchmark Results

This directory holds the frozen release artifact `bench.json` produced by
`hns_bench` on the named machine.

- The report is small: metrics, parameters, machine/build metadata, and dataset
  hashes.
- The SIFT dataset itself stays in gitignored `data/`; do not commit it here.
- `docs/results/bench.json` is the only report the README table and the Benchmark tab
  may cite.
- A local scratch run may write `data/bench.json`; promote it here only after the run
  is frozen, `dirty=false`, and the numbers are copied into the README.

There is no report yet. The README table stays `TBD` until the v1 release run.

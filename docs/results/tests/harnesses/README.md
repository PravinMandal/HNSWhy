# Standalone engine harnesses

These programs exercised the HNSW engine during the 2026-11-11 critic
iteration. They are not part of the CMake test target; the unit suite in
`backend/tests/` remains the maintained regression suite. They are kept here so
the measurements in `docs/reviews/final-iteration-2026-11-11.md` can be
reproduced.

Build and run from the repository root:

```bash
g++ -std=c++20 -O2 -I backend/src \
    docs/results/tests/harnesses/timing.cpp \
    backend/src/engine/hnsw.cpp backend/src/engine/brute_force.cpp \
    backend/src/engine/metrics.cpp backend/src/engine/vector_store.cpp \
    -o /tmp/hnsw_timing
/tmp/hnsw_timing
```

| File | What it checks |
|------|----------------|
| `fuzz.cpp` | Random graphs, inserts, removals, and queries. With `ef >= live count`, results must be exact. 40 seeds; stored output is `../hnsw-fuzz-2026-11-11.txt`. |
| `churn_validate.cpp` | 60 seeds × 600 random insert/remove operations, calling `validate()` after every operation. Stored output is `../hnsw-churn-validate-2026-11-11.txt`. |
| `timing.cpp` | Builds 100k vectors, measures clean search and the first search after an insert, then a duplicate-heavy 50k set. Stored output is `../hnsw-timing-2026-11-11.txt`. |
| `disconnect.cpp` | Builds a graph whose layer 0 is disconnected and measures fallback frequency, distance work, and duplicate-heavy cost against brute force. Stored output is `../hnsw-disconnect-2026-11-11.txt`. |
| `two_component_one_hot.cpp` | Builds disconnected one-hot graphs over 200 seeds and checks that every self-query still returns its own slot. Stored output is `../hnsw-two-component-2026-11-11.txt`. |
| `two_component_permuted.cpp` | Same check with 3,000 shuffled insertion orders. Stored output is `../hnsw-two-component-2026-11-11.txt`. |
| `churn.cpp` | Exploratory harness. It asserts that a self-query returns the node itself whenever every live node is reachable from `entry_`. That is stronger than HNSW promises at `ef=50` with `M=4..8`; both the pre-change engine and the current engine miss seed 100, slot 36. Kept for transparency, not as a pass/fail gate. |

The `hnsw-churn-current-2026-11-11.txt` output came from the current engine.
The `hnsw-churn-HEAD-2026-11-11.txt` output came from compiling the same harness
against the engine sources at commit `828d0de` (`git show 828d0de:...`). The
two files are byte-identical.

# SESSIONS — HNSWhy

One row per 4-hour session. Fill it in at the end of the session, before you close
the laptop. This is the only honest answer to "am I on track." Feeling is not data.

`LOC by me` = lines you wrote, not lines the agent wrote. Be strict; inflating this
defeats the purpose of the log.

| # | date | hours | LOC by me | what works now | what is broken | checkpoint passed |
|---|---|---|---|---|---|---|
| 1 | | | | | | `./build/hello` runs |
| 2 | | | | | | 3 hand-checked distance assertions |
| 3 | | | | | | p50 stable across runs |
| 4 | | | | | | 100k loaded, self-distance 0 |
| 5 | | | | | | **brute-force recall = 1.000** |
| 6 | | | | | | level decay explained with a number |
| 7 | | | | | | hand-drawn graph matches printed graph |
| 8 | | | | | | beam search correct at n=2,10,100 |
| 9 | | | | | | degree cap enforced |
| 10 | | | | | | cluster test: nearest-only <0.5, heuristic >0.9 |
| 11 | | | | | | **recall@10 ≥ 0.95 on 10k** |
| 12 | | | | | | ef sweep plotted |
| 13 | | | | | | descent benefit measured |
| 14 | | | | | | M × efConstruction sweep done |
| 15 | | | | | | **recall–QPS curve exists** |
| 16 | | | | | | unit tests green |
| 17 | | | | | | delete + reinsert works |
| 18 | | | | | | cluster test in CI |
| 19 | | | | | | **CI green and blocking** |
| 20 | | | | | | bench harness with warmup + RSS |
| 21 | | | | | | full SIFT-100k run, no blank CSV cells |
| 22 | | | | | | README has real numbers |
| 23 | | | | | | whiteboard, no notes |
| 24 | | | | | | Loom + DEMO.md + resume line |

## Weekly review

Every 6 sessions, answer these three in one line each:

1. Sessions completed / sessions planned:
2. Is the LOC-per-session number rising, flat, or falling?
3. Which checkpoint is furthest behind, and why?

## Stop conditions

If any of these, stop and get help rather than grinding:

- No runnable binary by session 4
- Recall still below 0.90 at session 14 and you're changing parameters rather than
  finding a bug
- More than 2 sessions spent on tooling
- Any committed function you cannot explain in one sentence

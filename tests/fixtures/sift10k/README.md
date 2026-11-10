# SIFT-10k fixture

A small, self-contained slice of SIFT for CI and loader checks. It is derived from
the standard SIFT-1M dataset, not from a different distribution.

| File | Shape | Meaning |
|---|---|---|
| `base_10k.fvecs` | 10,000 × 128 | first 10,000 base vectors |
| `queries_1k.fvecs` | 1,000 × 128 | first 1,000 query vectors |
| `groundtruth_ids.ivecs` | 1,000 × 100 | exact top-100 base IDs for each query |
| `groundtruth_dist.fvecs` | 1,000 × 100 | squared-L2 distances for those IDs |
| `query_ids.txt` | 1,000 lines | source query indices (`0`–`999`) |
| `SHA256SUMS` | — | checksums for every file above |

Ground truth was generated over this exact 10k base prefix with `hns_gt`, not copied
from the SIFT-1M answer key (which is only valid on the full 1M set).

Regenerate from `data/sift/`:

```bash
head -c 5160000 data/sift/sift_base.fvecs  > tests/fixtures/sift10k/base_10k.fvecs
head -c 516000  data/sift/sift_query.fvecs > tests/fixtures/sift10k/queries_1k.fvecs
./build/backend/hns_gt \
  --base tests/fixtures/sift10k/base_10k.fvecs \
  --queries tests/fixtures/sift10k/queries_1k.fvecs \
  --out-ids tests/fixtures/sift10k/groundtruth_ids.ivecs \
  --out-dist tests/fixtures/sift10k/groundtruth_dist.fvecs \
  --n 10000 --k 100
(cd tests/fixtures/sift10k && sha256sum base_10k.fvecs queries_1k.fvecs \
  groundtruth_ids.ivecs groundtruth_dist.fvecs query_ids.txt > SHA256SUMS)
```

The integration test `sift10k_fixture_bruteforce_matches_groundtruth` checks that
exact search over the fixture reproduces the stored IDs and distances. The 100k
benchmark still uses the full dataset under gitignored `data/`.

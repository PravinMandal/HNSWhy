#!/usr/bin/env bash
# Build the release binary, compute exact ground truth for a base prefix if it
# does not exist yet, then run the SIFT-100k benchmark and write
# docs/results/bench.json. No HTTP server or LLM is involved.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="${DATA_DIR:-$ROOT/data}"
PREFIX="${PREFIX:-100000}"
GT_K="${GT_K:-100}"
K="${K:-10}"
PASSES="${PASSES:-3}"
WARMUP="${WARMUP:-200}"
EF_GRID="${EF_GRID:-10,20,50,100,200}"
DATASET_NAME="${DATASET_NAME:-SIFT-100k}"
JOBS="${JOBS:-$(nproc)}"

BASE="$DATA_DIR/sift/sift_base.fvecs"
QUERIES="$DATA_DIR/sift/sift_query.fvecs"
GT_DIR="$DATA_DIR/sift100k"
GT_IDS="$GT_DIR/gt_ids.ivecs"
GT_DIST="$GT_DIR/gt_dist.fvecs"

for file in "$BASE" "$QUERIES"; do
    if [[ ! -f "$file" ]]; then
        echo "[run_bench] missing $file" >&2
        echo "[run_bench] run scripts/fetch_sift.sh first" >&2
        exit 1
    fi
done

echo "[run_bench] building release"
cmake --preset release >/dev/null
cmake --build --preset release -j "$JOBS"

mkdir -p "$GT_DIR"
if [[ ! -f "$GT_IDS" || ! -f "$GT_DIST" ]]; then
    echo "[run_bench] computing exact ground truth for the first $PREFIX vectors"
    "$ROOT/build/backend/hns_gt" \
        --base "$BASE" \
        --queries "$QUERIES" \
        --out-ids "$GT_IDS" \
        --out-dist "$GT_DIST" \
        --n "$PREFIX" \
        --k "$GT_K"
else
    echo "[run_bench] reusing $GT_IDS and $GT_DIST"
fi

echo "[run_bench] running benchmark"
"$ROOT/build/backend/hns_bench" \
    --base "$BASE" \
    --queries "$QUERIES" \
    --gt-ids "$GT_IDS" \
    --gt-dist "$GT_DIST" \
    --n "$PREFIX" \
    --k "$K" \
    --passes "$PASSES" \
    --warmup "$WARMUP" \
    --ef "$EF_GRID" \
    --output "$ROOT/docs/results/bench.json" \
    --dataset-name "$DATASET_NAME"

echo "[run_bench] wrote $ROOT/docs/results/bench.json"

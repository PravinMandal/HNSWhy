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
OUTPUT="$ROOT/docs/results/bench.json"

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
sha256_of() { sha256sum "$1" | awk '{print $1}'; }
BASE_SHA256="$(sha256_of "$BASE")"
QUERIES_SHA256="$(sha256_of "$QUERIES")"

# Ground truth depends on the base prefix, the query file, and k. Reusing a
# cache built for different inputs would silently compare recall against the
# wrong neighbours, so stamp the inputs and recompute on any mismatch.
GT_STAMP="$GT_DIR/gt.stamp"
GT_STAMP_TEXT="base_sha256=$BASE_SHA256
queries_sha256=$QUERIES_SHA256
prefix=$PREFIX
gt_k=$GT_K"
if [[ -f "$GT_IDS" && -f "$GT_DIST" && -f "$GT_STAMP" ]] &&
   [[ "$(cat "$GT_STAMP")" == "$GT_STAMP_TEXT" ]]; then
    echo "[run_bench] reusing $GT_IDS and $GT_DIST"
else
    echo "[run_bench] computing exact ground truth for the first $PREFIX vectors"
    "$ROOT/build/backend/hns_gt" \
        --base "$BASE" \
        --queries "$QUERIES" \
        --out-ids "$GT_IDS" \
        --out-dist "$GT_DIST" \
        --n "$PREFIX" \
        --k "$GT_K"
    printf '%s\n' "$GT_STAMP_TEXT" > "$GT_STAMP"
fi

# Provenance makes a frozen report traceable to one checkout and one set of
# input files. Hashes cover the full source files; base_prefix records how much
# of the base file the benchmark actually loaded.
RUN_ID="${RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$(git -C "$ROOT" rev-parse --short HEAD)}"
GIT_COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
if git -C "$ROOT" diff --quiet && git -C "$ROOT" diff --cached --quiet &&
   [[ -z "$(git -C "$ROOT" ls-files --others --exclude-standard)" ]]; then
    WORKTREE_DIRTY=0
else
    WORKTREE_DIRTY=1
fi
GT_IDS_SHA256="$(sha256_of "$GT_IDS")"
GT_DIST_SHA256="$(sha256_of "$GT_DIST")"
CXX_FLAGS_RELEASE="$(sed -n 's/^CMAKE_CXX_FLAGS_RELEASE:STRING=//p' "$ROOT/build/CMakeCache.txt" | head -n 1)"

EXTRA_FLAGS=()
if [[ "$WORKTREE_DIRTY" == "1" ]]; then
    EXTRA_FLAGS+=(--worktree-dirty)
fi

echo "[run_bench] running benchmark"
cd "$ROOT"
"$ROOT/build/backend/hns_bench" \
    --base "${BASE#"$ROOT"/}" \
    --queries "${QUERIES#"$ROOT"/}" \
    --gt-ids "${GT_IDS#"$ROOT"/}" \
    --gt-dist "${GT_DIST#"$ROOT"/}" \
    --n "$PREFIX" \
    --k "$K" \
    --passes "$PASSES" \
    --warmup "$WARMUP" \
    --ef "$EF_GRID" \
    --output "$OUTPUT" \
    --dataset-name "$DATASET_NAME" \
    --run-id "$RUN_ID" \
    --git-commit "$GIT_COMMIT" \
    --base-sha256 "$BASE_SHA256" \
    --queries-sha256 "$QUERIES_SHA256" \
    --gt-ids-sha256 "$GT_IDS_SHA256" \
    --gt-dist-sha256 "$GT_DIST_SHA256" \
    --cxx-flags-release "$CXX_FLAGS_RELEASE" \
    "${EXTRA_FLAGS[@]}"

echo "[run_bench] wrote $OUTPUT"

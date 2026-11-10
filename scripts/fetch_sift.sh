#!/usr/bin/env bash
# Fetch the SIFT-1M corpus used by the benchmark. The archive is ~168 MB and
# expands to ~530 MB. Safe to re-run: existing files are left untouched.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="${DATA_DIR:-$ROOT/data}"
URL="${SIFT_URL:-ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz}"
ARCHIVE="$DATA_DIR/sift.tar.gz"
DEST="$DATA_DIR/sift"

required=(
    "$DEST/sift_base.fvecs"
    "$DEST/sift_query.fvecs"
    "$DEST/sift_groundtruth.ivecs"
)

if [[ -f "${required[0]}" && -f "${required[1]}" && -f "${required[2]}" ]]; then
    echo "[fetch_sift] corpus already present in $DEST"
    exit 0
fi

command -v curl >/dev/null 2>&1 || {
    echo "[fetch_sift] curl is required" >&2
    exit 1
}

mkdir -p "$DATA_DIR"
if [[ ! -f "$ARCHIVE" ]]; then
    echo "[fetch_sift] downloading $URL"
    curl --fail --location --retry 3 --output "$ARCHIVE.part" "$URL"
    mv "$ARCHIVE.part" "$ARCHIVE"
fi

echo "[fetch_sift] extracting $ARCHIVE"
tar -xzf "$ARCHIVE" -C "$DATA_DIR"

for file in "${required[@]}"; do
    if [[ ! -f "$file" ]]; then
        echo "[fetch_sift] missing expected file after extraction: $file" >&2
        exit 1
    fi
done

echo "[fetch_sift] ready: $DEST"

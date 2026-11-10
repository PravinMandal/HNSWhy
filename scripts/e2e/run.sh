#!/usr/bin/env bash
# Runs the real API and browser checks against a fresh database.
#
# Usage:
#   scripts/e2e/run.sh                 # build if needed, fresh DB, port 8092
#   PORT=8094 scripts/e2e/run.sh       # choose a port
#   OUT=docs/results/e2e/latest scripts/e2e/run.sh
#
# The server is stopped automatically. Screenshots and the raw logs stay in
# $OUT so a failure can be inspected after the run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

PORT="${PORT:-8092}"
OUT="${OUT:-${TMPDIR:-/tmp}/hnswhy-e2e-$(date +%Y%m%dT%H%M%S)}"
BASE="http://127.0.0.1:$PORT"
SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

mkdir -p "$OUT"

if [[ ! -x build/backend/hnswhy_server ]]; then
    echo "[e2e] building release binaries"
    cmake --preset release >/dev/null
    cmake --build --preset release >/dev/null
fi

if [[ ! -f frontend/dist/index.html ]]; then
    echo "[e2e] building the frontend"
    (cd frontend && npm ci --silent && npm run build --silent)
fi

echo "[e2e] starting a fresh server on $BASE"
./build/backend/hnswhy_server \
    --db "$OUT/main.db" \
    --port "$PORT" \
    --samples-dir samples \
    >"$OUT/server.log" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 100); do
    if curl -sf "$BASE/api/health" >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "[e2e] server exited during startup; see $OUT/server.log" >&2
        cat "$OUT/server.log" >&2
        exit 1
    fi
    sleep 0.1
done
if ! curl -sf "$BASE/api/health" >/dev/null 2>&1; then
    echo "[e2e] server did not become healthy; see $OUT/server.log" >&2
    exit 1
fi

echo "[e2e] API checks"
BASE="$BASE" OUT="$OUT" scripts/e2e/api-e2e.sh | tee "$OUT/api-e2e.log"

echo "[e2e] browser checks"
BASE="$BASE" OUT="$OUT" node scripts/e2e/ui-e2e.cjs | tee "$OUT/ui-e2e.log"

echo "[e2e] artifacts: $OUT"

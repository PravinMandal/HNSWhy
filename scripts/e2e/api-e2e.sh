#!/usr/bin/env bash
# End-to-end API checks against a running HNSWhy server.
set -euo pipefail

BASE="${BASE:-http://127.0.0.1:8092}"
OUT="${OUT:-/tmp/opencode/hnswhy-final-e2e}"
PASS=0
FAIL=0

check() {
    local name="$1"
    shift
    if "$@"; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        FAIL=$((FAIL + 1))
    fi
}

status_is() {
    local path="$1" expected="$2"
    local actual
    actual="$(curl -s -o /dev/null -w '%{http_code}' "$BASE$path")"
    [[ "$actual" == "$expected" ]] || echo "  expected $expected, got $actual for $path"
    [[ "$actual" == "$expected" ]]
}

json_is() {
    local body="$1" filter="$2" expected="$3"
    local actual
    actual="$(jq -r "$filter" <<<"$body")"
    [[ "$actual" == "$expected" ]] || echo "  $filter: expected '$expected', got '$actual'"
    [[ "$actual" == "$expected" ]]
}

# --- read endpoints -------------------------------------------------------
HEALTH="$(curl -sf "$BASE/api/health")"
check "health reports ok" json_is "$HEALTH" '.status' 'ok'
check "health has database/store/providers" bash -c \
    'jq -e ".database == \"ok\" and .store.chunks > 0 and .embedding.available and .chat.available" <<<"$1" >/dev/null' _ "$HEALTH"

STATUS="$(curl -sf "$BASE/api/status")"
check "status exposes index dimensions" bash -c \
    'jq -e ".index.live > 0 and .index.dim == .embedding.dim and .sample_loaded" <<<"$1" >/dev/null' _ "$STATUS"

DOCS="$(curl -sf "$BASE/api/documents")"
check "documents list is seeded" bash -c 'jq -e "length >= 8" <<<"$1" >/dev/null' _ "$DOCS"
FIRST_ID="$(jq -r '.[0].id' <<<"$DOCS")"
DETAIL="$(curl -sf "$BASE/api/documents/$FIRST_ID")"
check "document detail has text and chunks" bash -c \
    'jq -e ".document.id > 0 and (.text | length) > 0 and (.chunks | length) > 0" <<<"$1" >/dev/null' _ "$DETAIL"

# --- ask ------------------------------------------------------------------
ASK="$(curl -sf -H 'Content-Type: application/json' \
    -d '{"question":"How does HNSW search work?","k":5}' "$BASE/api/ask")"
check "ask returns an answer with sources" bash -c \
    'jq -e "(.answer | length) > 0 and (.sources | length) > 0 and (.query_vector | length) == 256" <<<"$1" >/dev/null' _ "$ASK"
check "ask timings and models are present" bash -c \
    'jq -e ".timings_ms.total >= 0 and .models.embedding_model == \"local-hash-v1\"" <<<"$1" >/dev/null' _ "$ASK"

# --- map ------------------------------------------------------------------
MAP="$(curl -sf "$BASE/api/map?limit=50")"
check "map returns sampled points with vectors" bash -c \
    'jq -e ".sampled > 0 and .total_chunks >= .sampled and (.points[0].vector | length) == 256" <<<"$1" >/dev/null' _ "$MAP"

# --- benchmark passthrough ------------------------------------------------
BENCH="$(curl -sf "$BASE/api/benchmark")"
check "benchmark report is available" json_is "$BENCH" '.available' 'true'
check "benchmark report carries config and methods" bash -c \
    'jq -e ".report.config.k == 10 and (.report.methods | length) > 0" <<<"$1" >/dev/null' _ "$BENCH"

# --- upload JSON text -----------------------------------------------------
UPLOAD="$(curl -sf -H 'Content-Type: application/json' \
    -d '{"title":"API E2E notes","source":"e2e.md","text":"HNSWhy stores chunks in SQLite and keeps an HNSW graph in memory. The graph answers nearest-neighbour queries while SQLite remains the source of truth. Café naïve façade."}' \
    "$BASE/api/documents")"
NEW_ID="$(jq -r '.document.id' <<<"$UPLOAD")"
check "JSON upload returns a new document" bash -c '[[ "$1" -gt 0 ]]' _ "$NEW_ID"
check "uploaded document is searchable" bash -c \
    'jq -e ".answer | length > 0" <<<"$1" >/dev/null' _ \
    "$(curl -sf -H 'Content-Type: application/json' -d '{"question":"What is the source of truth?","k":3}' "$BASE/api/ask")"

# --- multipart upload -----------------------------------------------------
printf '# Multipart notes\n\nA markdown file uploaded through multipart form data.\n' > "$OUT/multipart.md"
MULTI="$(curl -sf -F "file=@$OUT/multipart.md;type=text/markdown" -F 'title=Multipart E2E' "$BASE/api/documents")"
MULTI_ID="$(jq -r '.document.id' <<<"$MULTI")"
check "multipart markdown upload succeeds" bash -c '[[ "$1" -gt 0 ]]' _ "$MULTI_ID"
check "multipart title is honoured" json_is "$MULTI" '.document.title' 'Multipart E2E'

# --- PDF upload -----------------------------------------------------------
PDF="$(curl -sf -F "file=@tests/fixtures/pdf-text.pdf;type=application/pdf" "$BASE/api/documents")"
PDF_ID="$(jq -r '.document.id' <<<"$PDF")"
check "PDF upload extracts text" bash -c \
    'jq -e ".document.chunk_count > 0 and (.document.char_count > 0)" <<<"$1" >/dev/null' _ "$PDF"

# --- error paths ----------------------------------------------------------
check "ask rejects non-JSON content type with 415" bash -c \
    '[[ "$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: text/plain" -d "{}" "$1/api/ask")" == "415" ]]' _ "$BASE"
check "ask rejects invalid JSON with 400" bash -c \
    '[[ "$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Content-Type: application/json" -d "{not json" "$1/api/ask")" == "400" ]]' _ "$BASE"
check "upload rejects unsupported file type" bash -c \
    '[[ "$(curl -s -o /dev/null -w "%{http_code}" -F "file=@/etc/hostname;filename=bad.exe" "$1/api/documents")" == "415" ]]' _ "$BASE"
check "upload rejects NUL bytes with 422" bash -c \
    '[[ "$(curl -s -o /dev/null -w "%{http_code}" -H "Content-Type: application/json" -d "{\"title\":\"NUL\",\"text\":\"a\\u0000b\"}" "$1/api/documents")" == "422" ]]' _ "$BASE"
check "map rejects an invalid limit" status_is '/api/map?limit=0' 400
check "missing document is 404" status_is '/api/documents/999999' 404

# --- delete ---------------------------------------------------------------
check "delete removes the uploaded document" bash -c \
    '[[ "$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$1/api/documents/$2")" == "200" ]]' _ "$BASE" "$NEW_ID"
check "deleted document is gone" status_is "/api/documents/$NEW_ID" 404
check "delete of the multipart document succeeds" bash -c \
    '[[ "$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$1/api/documents/$2")" == "200" ]]' _ "$BASE" "$MULTI_ID"
check "delete of the PDF document succeeds" bash -c \
    '[[ "$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$1/api/documents/$2")" == "200" ]]' _ "$BASE" "$PDF_ID"

# --- single-writer lock ---------------------------------------------------
LOCK_STATUS=0
./build/backend/hnswhy_server --db "$OUT/main.db" --port 8093 --no-sample \
    > "$OUT/second-server.log" 2>&1 || LOCK_STATUS=$?
check "second server on the same database is refused" bash -c \
    '[[ "$1" -ne 0 ]] && grep -q "already open by another hnswhy process" "$2"' _ "$LOCK_STATUS" "$OUT/second-server.log"

echo
echo "$PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]

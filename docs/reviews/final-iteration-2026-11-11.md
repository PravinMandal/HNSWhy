# Final critic iteration — 2026-11-11

This was the last full review pass requested in `docs/goal.md` §10. Five
independent critic agents reviewed the implementation: the HNSW engine, the test
suite, the backend API/database/security surface, the frontend UX and
accessibility, and C++ readability/safety. Every finding below was checked
against the code before being accepted. Findings that were accepted have a fix
and a verification command; nothing in this document is a review that did not
run.

## HNSW engine critic

Scope: `backend/src/engine/hnsw.{h,cpp}`, `types.h`, the benchmark CLI, the
unit tests, and the HNSW documentation. The critic built its own timing,
disconnect, churn, and fuzz harnesses against the engine rather than reading
only the test suite.

| # | Severity | Finding | Verdict | Resolution |
|---|----------|---------|---------|------------|
| 1 | P1 | `insert()` always marked layer-0 connectivity dirty, so the next search ran a full BFS over the graph. Measured first search after an insert: **4.921 ms** versus **0.0053 ms** on a clean graph. | Accepted | The dirty flag is now set only when the insert can actually change reachability: the graph was already untrusted, the insert became the new entry point, the new node got no layer-0 in-edge, or a selected layer-0 neighbour was already deleted. |
| 2 | P1 | A disconnected graph triggered a complete exact scan on every query. Measured **2.295 ms** against **0.305 ms** for brute force on tie-heavy data. | Accepted | `recomputeConnectivity()` now records exactly the live slots the layer-0 walk from `entry_` cannot reach in `unreachable_live_`. `search()` scans only those slots and merges them into the beam, deduplicated against the beam ids because upper-layer descent can start in a different layer-0 component. |
| 3 | P2 | The exact fallback collected and sorted every live candidate even though only `k` are returned. | Accepted | The fallback uses `std::nth_element` for the first `k` and sorts only those `k`. |
| 4 | P2 | The benchmark printed the requested `efSearch` even when the engine clamped it to the slot count, overstating the search width. | Accepted | `--ef` values below `k` are rejected at startup and each row is labelled with `min(requested, N)`. The comma-list parser moved to `bench/cli.h` and is unit-tested. |
| 5 | P2 | Layer-0 back-edge repair can perform up to `M0` repairs of `O(M0²)` pairwise checks each, beyond the complexity documented in the spec. | Accepted (documentation) | HNSW-SPEC §12 now states the `O(M0³)` layer-0 worst case and why the repair exists. The algorithm was not changed because that would invalidate the frozen benchmark. |
| 6 | P2 | Tests called `validate()` before and after loops, but never between individual inserts and removals. | Accepted | Added `hnsw_validate_holds_after_every_incremental_change`: 50 random inserts with `validate()` after each, then removal of every other live node with `validate()` after each. |
| 7 | P2 | The docs described swap-erase pruning, const `search` overloads, and a different diversity predicate than the code. | Accepted | `docs/ARCHITECTURE.md` and `docs/HNSW-SPEC.md` now match the implementation: `std::copy` overwrite in order, non-const search helpers, and the exact keep/reject predicate. |

## Testing critic

Scope: every test target, the API server, persistence, chunking, providers, the
benchmark report, and the frontend. The critic's standard was that a behaviour
guarded by code must have a test that fails if the guard is removed.

| # | Severity | Finding | Verdict | Resolution |
|---|----------|---------|---------|------------|
| 1 | P1 | Sample seeding could write over an existing user library. | Accepted | `application_does_not_seed_samples_into_an_existing_library` creates user data, removes `app_initialized`, reopens with `load_sample=true`, and asserts the library is untouched. |
| 2 | P1 | `parse_endpoint` had no tests. | Accepted | New `test_http.cpp` covers scheme/prefix normalization, secure detection, the bare `host:port` form, and rejection of userinfo, queries, fragments, and malformed origins. |
| 3 | P1 | The newer-schema guard had no test. | Accepted | `document_store_rejects_a_newer_schema` sets `PRAGMA user_version = 2` and asserts the reopen fails with a "newer" error. |
| 4 | P1 | The HTTP server size limits had no tests. | Accepted | Tests cover a 4 MB + 1 text upload returning `DOCUMENT_TOO_LARGE`/413, a 20 MB + 1 upload returning 413, and the exact-limit success cases. |
| 5 | P2 | The map point cap by embedding dimension had no test. | Accepted | `application_caps_map_points_by_embedding_dimension` uses 65536-dimensional vectors and asserts the returned point count and payload size. |
| 6 | P2 | The chunk-overflow guard had no test. | Accepted | `chunker_rejects_documents_over_max_chunks` verifies the boundary and the 422 error. |
| 7 | P2 | `BenchmarkPage` unavailable, error, and loading states had no tests. | Accepted | New `BenchmarkPage.test.tsx` covers the unavailable command, retry, rejected request, and pending status region. |
| 8 | P2 | Database file permissions had no test. | Accepted | `document_store_restricts_files_to_the_owner` checks the db and lock files (and WAL/SHM when present) are owner-read/write only. |

## Frontend UX and accessibility critic

Scope: every page and shared component, the style tokens, the Vitest suite, and
the four committed E2E screenshots. The critic checked keyboard and screen-reader
paths, not only the rendered layout.

| # | Severity | Finding | Verdict | Resolution |
|---|----------|---------|---------|------------|
| 1 | P2 | Navigating from the mobile drawer closed it but left focus on the menu button: the heading was focused while the workspace was still inert, then the drawer cleanup restored the trigger. | Accepted | Route changes now set a pending-focus flag, close the drawer, and focus the new heading only after `mobileOpen` is false. The drawer cleanup skips its restore when navigation owns focus. Covered by the new `AppShell.test.tsx`. |
| 2 | P2 | Deleting a document removed the focused row before the confirmation dialog closed, so the dialog restored focus to a detached button and focus fell to `<body>`. | Accepted | `Modal` restores the opener only when it is still connected; otherwise it focuses `#main-content`. Covered by the new delete-focus test in `DocumentsPage.test.tsx`. |
| 3 | P2 | The light-theme “Try again” button was 4.06:1 against the error banner. | Accepted | `.error-notice .btn--quiet` uses `--accent-strong`; measured contrast is now 5.79:1 (5.70:1 on hover). |
| 4 | P2 | Activating “Show full chunk” unmounted the button, dropping keyboard focus to `<body>`. | Accepted | The expander stays mounted, changes its label between “Show full chunk” and “Show less”, and carries `aria-expanded`. Covered by `AnswerView.test.tsx`. |
| 5 | P3 | A file-validation error survived switching to Paste text, leaving the textarea marked invalid for an unrelated error. | Accepted | Both mode switches clear the error. Covered by `UploadDialog.test.tsx`. |
| 6 | P3 | The Documents page showed a measured zero and “Nothing here yet” while the first request was still loading. | Accepted | The count renders an em dash and the description says “Loading documents…” while the list is pending. Covered by `DocumentsPage.test.tsx`. |
| 7 | P3 | The 720-unit recall chart scaled into a 390 px card made its labels about 4 px tall. | Accepted | On narrow screens the chart keeps a 640 px minimum width inside a focusable horizontal scroll region and uses larger tick/axis text. |
| 8 | P3 | The mobile-navigation, delete-focus, and chunk-expander paths had no regression tests. | Accepted | Added `AppShell.test.tsx` plus focused cases in `DocumentsPage.test.tsx` and `AnswerView.test.tsx`; the frontend suite is now 67 tests in 17 files. |

## Backend API, database and security critic

Scope: `backend/src/api/`, `backend/src/product/{app,document_store,chunker,providers}.cpp`,
`backend/src/main.cpp`, the SQLite schema, and the deployment notes. The critic
looked for ways an unauthenticated local demo becomes a remote compromise when it
is bound to a LAN address or put behind a proxy.

| # | Severity | Finding | Verdict | Resolution |
|---|----------|---------|---------|------------|
| 1 | P1 | The server accepted any `Host` header, so a DNS-rebinding page could reach the loopback API with the browser's own origin. | Accepted | A pre-routing handler rejects a missing `Host` with `400 MISSING_HOST` and any name outside the allowlist with `403 FORBIDDEN_HOST`. The allowlist always contains `localhost`, `127.0.0.1`, and `::1`, plus the concrete bind host and any `HNSWHY_ALLOWED_HOSTS` / repeatable `--allowed-host` names. Ports are ignored when comparing names. Binding to a wildcard address without extra names logs a startup warning. Covered by `api_rejects_foreign_hosts_and_cross_site_writes`,
`api_accepts_configured_extra_host_names`, and
`config_reads_allowed_hosts_from_environment_and_flags`. |
| 2 | P2 | State-changing requests had no origin check; a cross-site form or `fetch` could upload or delete documents on a reachable server. | Accepted | `POST`/`PUT`/`PATCH`/`DELETE` now reject a `Sec-Fetch-Site` that is neither `same-origin` nor `none`, and an `Origin` whose authority does not equal the request `Host`, with `403 CROSS_SITE_REQUEST`. The check is in the same pre-routing handler, so no route can forget it. Covered by the same test. |
| 3 | P3 | SQLite created the database with the process umask before `DocumentStore` narrowed it, leaving a short window where the file was world-readable. | Accepted | `main` sets `umask(0077)` before any file is opened, and `DocumentStore` pre-creates the database with `O_CREAT | O_CLOEXEC` mode `0600` before taking the lock. WAL, SHM, temporary PDFs, and logs inherit the owner-only umask. The existing permission test still passes. |
| 4 | P3 | When seeding was skipped because the active provider was not the offline hash embedder, `app_initialized` was still written, so the sample library never loaded after switching back to hash. | Accepted | Seeding now records `seeding_skipped`; the initialization marker is written only when seeding actually finished or was not needed. `application_retries_sample_seed_after_a_non_hash_start` starts with Ollama, asserts an empty library and no marker, reopens with hash, and asserts one seeded document. |
| 5 | P3 | PDF extraction had no concurrency or resource bound; several large uploads could spawn unlimited `pdftotext` processes. | Accepted | A two-slot `std::counting_semaphore` limits concurrent extractions (a 5 s wait returns `503 SERVER_BUSY`), and the child shell applies `ulimit -v 1048576` and `ulimit -t 30` plus `timeout 30s`. |
| 6 | P3 | The benchmark report was checked with `stat` and then reopened, a TOCTOU window, and the read was unbounded. | Accepted | `benchmark_json()` opens once, `fstat`s the descriptor, requires a regular file, caps the size at 10 MB, and reads exactly `st_size` bytes with `EINTR` handling. |
| 7 | P3 | Host, Origin, and static-file traversal behaviour had no tests, so a regression would be silent. | Accepted | Three tests were added: foreign/loopback Host and cross-site Origin/Fetch-Site, configured extra host names, and traversal paths (`..`, encoded `..`) against a root with a sibling secret file. |

## C++ readability and safety critic

Scope: `backend/src/engine/`, `backend/src/product/`, `backend/src/api/routes.cpp`,
and the test harness. The critic read the code as a reviewer who has to maintain
it, not only as a correctness checker.

| # | Severity | Finding | Verdict | Resolution |
|---|----------|---------|---------|------------|
| 1 | P2 | A `std::bad_alloc` in the middle of `insert()` left a half-linked node in the graph with no way to detect it, so later searches could read a corrupt structure. | Accepted | Public `insert()` validates its preconditions, calls `insertImpl()`, and sets `poisoned_` on any exception. Every later mutating or query entry point calls `requireUsable()` and throws `std::logic_error` telling the caller to rebuild. Invalid preconditions throw before the try block, so they do not poison a healthy graph; `hnsw_rejects_an_invalid_insert_without_poisoning_the_graph` covers that. |
| 2 | P2 | The layer-0 beam was seeded only from the last upper-layer node. After upper-layer descent that node can sit in a different layer-0 component than `entry_`, so the guaranteed entry component was never visited. | Accepted | When the connectivity cache says the graph is disconnected, the layer-0 beam starts from both `entry` and `entry_`; when it is connected, only `entry` is used to keep the measured search path unchanged. The two temporary harnesses (one-hot vectors, permuted components) do not produce a deterministic public-API failure on the old code, so this is a targeted correctness fix without a fabricated regression test. |
| 3 | P3 | `stats.exact_fallbacks` incremented once per fallback scan, so a query that scanned both the unreachable set and the full store counted twice. | Accepted | A `used_exact_scan` flag is set in either branch and the counter increments once at the end; the field comment now says "queries, not scans". Tests assert exactly `1`. |
| 4 | P3 | `get_chunks` built one `IN (...)` list with one placeholder per requested id; a large retrieval could exceed SQLite's variable limit. | Accepted | The lookup batches 500 ids per statement and preserves caller order. `document_store_fetches_more_chunks_than_one_sql_batch` requests 1,200 chunks in reverse. |
| 5 | P3 | An OpenAI-compatible response with an `index` above `INT_MAX` was read with `get<int>()`, which throws nlohmann's own `out_of_range` and surfaces as a 500 instead of a provider error. | Accepted | The index is read as `long long`, range-checked against the batch size, and any duplicate/out-of-range value raises `ProductError` with status 502. `openai_compatible_client_rejects_an_out_of_int_range_index` uses `LLONG_MAX`. |
| 6 | P3 | Concurrency between an in-flight upload and concurrent questions was only reasoned about, not tested. | Accepted | `application_asks_while_an_upload_is_in_flight` runs 12 questions on one thread while the main thread uploads four documents; it asserts zero failures and that the store and live index agree at the end. |

## Verification after the fixes

Full commands and raw output are stored in `docs/results/tests/`; the
standalone harness sources and build commands are in
`docs/results/tests/harnesses/`.

- Release C++ suite: **166/166 passed** with
  `HNSWHY_SIFTSMALL_DIR="$PWD/data/siftsmall"`.
- ASan + UBSan suite (`build/dev`): **166/166 passed**, no sanitizer reports.
- CI preset (`build/ci`): configured, built, and passed `ctest`.
- Frontend type-check: `tsc --noEmit` clean.
- Frontend unit tests: **67/67 passed in 17 files**.
- API end-to-end: **26/26 passed** on a fresh database, port 8105.
- Browser end-to-end: **16/16 passed** in headless Chromium on port 8105. Final
  state was 8 documents / 16 chunks; the four screenshots were regenerated.
- HNSW fuzz harness: 40 seeds, `failures=0`, including exactness checks with
  `ef >= live count`.
- HNSW churn harness with `validate()` after every op: 60 seeds × 600 ops,
  `bad=0`.
- HNSW two-component probe: the old single-seed and new dual-seed code both pass
  the harness; no deterministic public-API reproducer was found, so fix 2 above
  is kept as a targeted guard and not claimed as a measured regression. Sources
  and output are in `docs/results/tests/harnesses/two_component_*.cpp` and
  `docs/results/tests/hnsw-two-component-2026-11-11.txt`.

### Effect of the two P1 engine fixes

| Measurement | Before | After |
|-------------|--------|-------|
| First search after one insert | 4.921 ms | 0.013 ms median |
| Clean-graph search | 0.0055 ms | 0.0055 ms |
| Disconnected fallback frequency | every query | 1 of 8,000 queries |
| Distances computed per fallback | all live vectors | 1,274 |
| Duplicate-heavy N=100k query | 7.5× brute force | 1.50× brute force (0.468 ms vs 0.312 ms) |

## Deliberately not treated as a defect

The critic's exploratory `churn` harness also checks that a self-query returns
the query node itself, with no fallback, whenever every live node is reachable
from `entry_`. It reports `SILENT WRONG` at seed 100, step 0, slot 36, and
returns node 61. The same harness compiled against the pre-change engine
(`git show 828d0de:backend/src/engine/...`) reports byte-identical output: same
seed, step, slot, entry point, live count, and returned node. It is therefore
not a regression from the connectivity work.

The expectation is stronger than the HNSW contract: the graph is directed, the
beam starts at a node chosen by upper-layer descent, and that configuration
uses `M` as low as 4 and `efConstruction` as low as 4. Approximate search at
`ef=50` is allowed to miss a neighbour on such a graph. The contract that does
matter — exact results when `ef >= N` — is enforced by the fuzz harness and the
166-test suite, and the shipped defaults (`M=16`, `efConstruction=200`) are
covered by the siftsmall and sift10k recall gates. No engine change was made to
satisfy the weaker harness assumption.

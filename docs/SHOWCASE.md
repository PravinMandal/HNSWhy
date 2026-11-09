# VectorForge — Product & Showcase

> The product UI is the showcase. The old three-page static site is retired.
>
> Read `ARCHITECTURE.md` §3 and §6 first. This file is the design for the four product
> tabs and the demo. It adds no index logic.
>
> **The one rule:** the UI never computes a benchmark number. It renders `bench.json`
> through `/api/benchmark`. If a number is not in the report, it does not appear on a
> chart, a table, or a slide.

---

## 0. Why the product is the showcase

There are three evaluation moments:

| Moment | Time | What they look at | What wins |
|---|---|---|---|
| Resume / GitHub | 5 seconds | repo name, README top | one clear product sentence + one real number |
| Phone screen | 15 minutes | shared screen | a 60-second tour that actually works |
| Onsite | 20 minutes | whiteboard | your head, not your laptop |

A static trace page can show one query. A working product shows the whole loop:
upload, retrieve, answer, cite, delete, restart. The benchmark tab shows the engine is
real. That is a stronger story than a separate showcase site, and it is one less
frontend to build.

The product is not a generic SaaS dashboard. It is a technical tool with four jobs:

1. **Documents** — get text into the index.
2. **Ask** — retrieve and answer with evidence.
3. **Map** — show that the embeddings are real.
4. **Benchmark** — show that the engine is measured.

---

## 1. Site map

```text
/            Documents tab (default)
/ask         Ask tab
/map         Map tab
/benchmark   Benchmark tab
```

The app is a single-page React app; the tabs can be client-side routes or a tab
switcher. The URL should be shareable enough to open a tab, but there is no auth and no
per-user state in v1.

No separate `/trace`, `/prune`, `/about`, or `/playground` routes. The prune
explanation is a panel in the Benchmark tab or a README section. About/fine print is a
footer.

---

## 2. Documents tab

**Job:** make it obvious what is indexed and let the user remove it.

### Layout

- Header: "Documents" + a primary "Add document" button.
- Upload area: drag-and-drop `.txt`/`.md` or paste text; title field; "Index" button.
- Status line: one "Indexing…" state while the request runs; on success show the chunk
  count returned by the API. On failure, the error message names the stage that failed
  (validation, embedding, or database). Do not fake a multi-stage progress bar.
- Document list: title, chunk count, created time, delete action. Delete asks for
  confirmation; the row is removed permanently (the in-memory graph marks it deleted).
- Empty state: "No documents yet. Upload a `.txt` or `.md`, or paste text."

### Behavior

- Upload accepts `.txt` and `.md` only in v1. PDF is deferred; do not pretend to
  support it.
- Text limit: 2 MB per upload.
- On submit: POST `/api/documents`; show progress; on success, refresh the list.
- Show the chunk count returned by the API; do not estimate it from a word count in the
  browser. Chunk size is a backend constant.
- Delete: confirmation dialog; DELETE `/api/documents/{id}`; remove from list; the
  backend deletes the SQLite document row (cascade removes chunks and embeddings) and
  calls `HNSW::remove` for the affected slots.
- If Ollama is down: show a clear message with the `ollama serve` command; do not
  insert a fake document.
- If embedding fails halfway: no partial document. The backend transaction rolls back.

### What not to show

- A fake "vector count" that is not from the API.
- A "demo document" that was never embedded.
- A 3D preview of the document. The Map tab is the visual.

---

## 3. Ask tab

**Job:** answer a question from the uploaded documents, with evidence.

### Layout

- Question input; "Ask" button; example questions if the corpus is known.
- Answer panel: the LLM answer, with numbered citations `[1]`, `[2]`.
- Sources panel: each cited chunk with document title, chunk text, distance, and chunk
  ID. Clicking a citation scrolls to the source.
- Retrieved panel (collapsible): all top-k chunks, including ones the answer did not
  cite, with distances.
- Timing line: embed ms, search ms, generate ms, total ms.

### Behavior

- POST `/api/ask` `{ question, k }`.
- Backend embeds, searches, retrieves, prompts, generates, returns
  `{ answer, sources, retrieved, timings }`.
- If the index has no live chunks: "I could not find this in the uploaded documents."
  No LLM call. Do not answer from general knowledge.
- If the index has chunks, retrieve and prompt. If the model refuses because the sources
  do not answer the question, show that refusal with the retrieved sources. A grounded
  refusal is a valid result, not an error.
- Do not apply a hard distance cutoff in v1; it is embedding-model dependent and
  uncalibrated.
- If Ollama is down: `503` with a fixable message.
- If the model returns an answer with no citations, show a warning: "The model did not
  cite a source; treat this as ungrounded." Do not hide it.
- The answer should be rendered as Markdown if the model returns Markdown, but
  sanitize it. No raw HTML injection.
- The question and answer are not persisted in v1. That is a product limitation, not a
  bug to hide.

### Honesty rules

- The citation is the evidence. A fluent answer without a citation is not a product
  success.
- Do not silently retry with a different prompt and present only the good answer.
- Do not use a hardcoded demo answer when the backend fails.
- Do not call a general-purpose model without the retrieved context and label it RAG.

---

## 4. Map tab

**Job:** show that the embeddings are real and that retrieval has geometry.

### Layout

- 2D scatter plot of sampled chunks; points colored by document (or one accent color
  if there are too many documents).
- Query marker and retrieved-hit markers, with labels.
- Legend: point = chunk, ring = query, filled = retrieved.
- Caption: "2D PCA projection of real embeddings; distances are approximate. Sample:
  N of M chunks."
- Optional document filter; optional "show only hits" toggle.

### Behavior

- GET `/api/map?limit=1000` returns sampled chunk vectors + metadata.
- Frontend computes 2D PCA in `lib/pca.js`; no server-side projection in v1.
- If the corpus is small, show all points; if large, sample deterministically (stable
  seed) so the map does not jump between reloads.
- Highlight the query and the current Ask tab's retrieved chunks if available.
- If there are no documents, show an empty state; do not draw a fake cloud.
- The map is not a benchmark. Do not label clusters as "good" or "bad" retrieval.

### Why 2D and not 3D

3D adds occlusion, camera controls, and a dependency for a screenshot that is not more
truthful. A 2D projection already shows the only thing the product needs: the points
come from real embeddings, and the query lands near its retrieved chunks.

---

## 5. Benchmark tab

**Job:** show the engine's measured quality without turning the product into a
benchmark runner.

### Layout

- Header: dataset, `N`, `dim`, `k`, query count, build/query parameters.
- Hero row: `R@10`, QPS, p95, build time, RSS, speedup.
- Recall–QPS curve: one point per measured `efSearch` value from the same build; hover
  shows `efSearch` and metrics; run metadata is in the header. The pre-registered SLO
  point is marked.
- Table: all measured points, including dominated ones; no interpolation.
- Command block: the exact `hns_bench` command.
- Metadata: commit, dirty flag, CPU, RAM, compiler, flags, thread count, dataset
  hashes.
- "Why diversity?" panel: the two-cluster prune explanation, with a link to
  `HNSW-SPEC.md` §7.1–§7.2. This replaces the old `/prune` page.
- Ablation: not in v1. The two-cluster test is the v1 evidence for the heuristic; an
  optional nearest-only build is a later experiment, not a panel in this tab.

### Behavior

- GET `/api/benchmark` reads `docs/results/bench.json` (or the configured report path)
  and returns it.
- If no report exists: show "No benchmark report. Run `scripts/run_bench.sh` to
  produce one." Do not invent a chart.
- If the report is from a different commit than the running server, show a warning.
- The tab never runs a 100k build in the request.
- The UI may format numbers, but it must not recompute recall or speedup from raw
  query data; those come from the report.

### What not to show

- A "500×" headline that is not in `bench.json`.
- A curve with interpolated points.
- A 1M row that was not measured.
- A "production-grade" badge. Say "portfolio v1" if a label is needed.

---

## 6. API and data contract

The product API is defined in `ARCHITECTURE.md` §7. The UI uses only these endpoints:

| UI action | Endpoint |
|---|---|
| health/status | `GET /api/health`, `GET /api/status` |
| upload/paste | `POST /api/documents` |
| list documents | `GET /api/documents` |
| delete document | `DELETE /api/documents/{id}` |
| ask | `POST /api/ask` |
| map | `GET /api/map?limit=1000` |
| benchmark | `GET /api/benchmark` |

The benchmark payload is `bench.json` from `plan.md` §8. The map payload is sampled
vectors, not precomputed coordinates; PCA is frontend code.

Error shape is consistent:

```json
{
  "error": {
    "code": "OLLAMA_UNAVAILABLE",
    "message": "Ollama is not reachable at localhost:11434. Start it with `ollama serve`."
  }
}
```

The UI should map error codes to actionable states, not dump a raw stack trace.

---

## 7. Frontend stack and design

### Stack

- Existing VectorForge React/Vite app, refactored to the four tabs in this document
  (see `ARCHITECTURE.md` §3.1).
- `api.js` for all HTTP calls and error normalization.
- `lib/pca.js` for 2D PCA.
- SVG for the benchmark curve; Canvas or SVG for the map (a few thousand points).
- No Next.js, no server-side rendering, no charting SaaS.
- No fake embedding function. The old `textToEmbedding` with `Math.random()` jitter is
  deleted, not hidden behind a flag.

### Visual direction

**Technical report, not SaaS landing page.**

- Near-black background, off-white text, one amber accent for retrieval, cyan for
  sources/hits, red for misses.
- Monospace for numbers; sans for prose.
- No hero image, gradients, glassmorphism, or stock illustrations.
- Every benchmark number has a caption with `run_id` or "not measured."
- Motion 150–250 ms; respect `prefers-reduced-motion`.
- Color is not the only carrier of meaning; use labels and shapes.
- The Documents/Ask flow works on a 390 px phone. The map can say "best on desktop."

### Performance

- The app shell should load fast; the benchmark JSON is small.
- The map payload is capped at 2,000 points; default 1,000. If it is slow, reduce the
  default, not the truth.
- No analytics, cookies, API keys in the frontend, or hidden network calls.

---

## 8. The 60-second demo

Fill every placeholder from the real `bench.json`; never say a number that is not in
the artifact.

1. **0–10 s — product.** "This is VectorForge. I upload a document, it gets chunked and
   embedded locally, and the vectors go into an HNSW index I wrote from scratch in
   C++."
2. **10–25 s — ask.** Upload a short doc; ask a question that is answerable only from
   it. Show the answer and click a citation. "The answer is grounded in these chunks.
   If the index finds nothing, it says so instead of inventing an answer."
3. **25–35 s — map.** "These are the real embeddings, projected to 2D with PCA. The
   query and its retrieved chunks are highlighted. This is a visualization, not a
   benchmark."
4. **35–50 s — benchmark.** "The same engine is measured on 100,000 SIFT vectors:
   R@10 R% at Q QPS, p95 P ms, B seconds to build, M MB RSS. Exact search does E QPS.
   Every point is a real run; the command and commit are here."
5. **50–60 s — close.** "The engine is single-threaded, L2, and rebuilt from SQLite on
   startup. The next step would be a graph snapshot or a concurrent read path, not a
   1M sweep."

If the interviewer asks for the pruning detail, open the "Why diversity?" panel and
draw the two-cluster example. Do not open a separate static site; there isn't one.

**Resume bullet template:**

> Built a RAG app backed by a from-scratch C++20 HNSW engine: document upload,
> SQLite persistence, Ollama embeddings, cited answers, and a 2D embedding map.
> Benchmarked the same engine against exact search on 100k SIFT: R@10 0.9X, X QPS vs
> Y exact (Z×), p95 W ms, B s build, M MB RSS. Demo: `<url>`.

Three numbers maximum; CPU/RAM and the full table are in the README.

---

## 9. Definition of done

- Documents: upload/paste, list, delete, restart; no partial documents.
- Ask: answer with citations; no-hit path; Ollama-down path; no fake fallback.
- Map: real embeddings, deterministic sample, query/hits highlighted, projection label.
- Benchmark: real `bench.json`, table/curve, command, metadata, no invented numbers.
- README: product screenshot or short recording, measured table, limitations, run
  commands.
- A 60-second demo works without editing code or faking data.
- No fake embeddings, no hardcoded speedup, no "production-grade" claim.
- The app is usable on a phone for Documents/Ask; the map can be desktop-first.

A public deployment is a stretch in v1. A local documented demo plus a recording is
enough to show the product; a public URL is better if it can run Ollama reliably.

---

## 10. Non-goals

| Not this | Because |
|---|---|
| A static three-page showcase | the product is the showcase (D12) |
| A 3D map | cosmetic; 2D shows the same truth (D14) |
| A KD-tree tab | high-dimensional decoration; one engine path (D14) |
| A live 100k benchmark in the API | noisy and slow; CLI report only (D11) |
| A trace-replay page | not needed for the product story; optional later |
| Auth, teams, billing | no user need in v1; adds a security surface |
| Agents, Graph RAG, tool calling | a different project |
| A fake demo fallback | destroys the only thing the project is selling: trust |
| A dashboard with twenty charts | one curve, one table, four tabs is enough |

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| Ollama is slow/unavailable during the demo | health check; clear 503; pre-warm; record a backup demo |
| The map looks like a random cloud | label it as PCA; show sample size; highlight hits |
| The answer cites nothing | show a warning; do not present it as grounded |
| Benchmark JSON is from an old commit | show commit/dirty; warn on mismatch |
| Frontend scope creep | four tabs; D20 cut list; no static site |
| Fake data sneaks back in | delete `textToEmbedding`; code review; tests with a fake Ollama only |
| The product hides a weak engine | benchmark tab shows real numbers and limitations |
| The demo is too long | 60-second script; benchmark tab is the backup for questions |

---

## 12. What this requires from the engine and API

Only three things, all cheap if done early:

1. **A stable slot-to-chunk mapping.** The API returns chunk IDs; the engine returns
   slots. Keep the mapping in one place.
2. **Mark-deleted search filtering.** The UI must never show a deleted chunk as a
   source.
3. **A benchmark report endpoint.** The UI reads `bench.json`; it does not run the
   benchmark.

Everything else in the engine is unchanged. The product starts after the engine's
small-fixture tests pass, not after a perfect UI. **Measure first, show second; make it
work, then make it pretty.**

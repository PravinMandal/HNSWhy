# HTTP API and error contract

The HNSWhy server exposes a small JSON API for health, documents, retrieval, maps, and benchmark reporting. The built frontend is served from the same process, so the browser can use relative URLs without a proxy. Errors use one consistent shape: an object with an error code and a human-readable message.

## Endpoints

`GET /api/health` reports process liveness. `GET /api/status` returns the embedding provider, model, dimension, index slot count, live and tombstoned vector counts, store totals, query `efSearch`, and whether sample data was loaded.

`GET /api/documents` lists documents with chunk counts and character counts. `GET /api/documents/{id}` returns the full document plus its chunks. `POST /api/documents` accepts multipart file uploads or a JSON body containing a title and text. PDF extraction runs through `pdftotext`; Markdown and plain text are read directly. `DELETE /api/documents/{id}` removes the document and tombstones its vectors.

`POST /api/ask` accepts a question, an optional `k`, and an optional `efSearch`. It returns a grounded answer, numbered sources with the retrieved text and scores, retrieval timings, provider information, and the query vector used for retrieval. `GET /api/map` returns a bounded random sample of chunk vectors with document identifiers and short previews. `GET /api/benchmark` returns the recorded JSON benchmark report or a structured unavailable message.

## Uploads and validation

Uploads are checked for a supported extension, a non-empty body, and a size limit before extraction. A document with no extractable text is rejected with a validation error rather than an empty chunk set. Query parameters are parsed defensively: invalid numbers, an unsupported provider, or a dimension mismatch produce a clear error instead of a crash.

The server limits request body size and rejects unknown routes with a JSON 404. Static assets are served with appropriate content types, and API routes are matched before the static fallback so a missing endpoint never returns the single-page application shell.

## Operational contract

One application mutex serializes document writes, index updates, and searches. Provider HTTP calls are created per request and never share mutable client state. Answers are generated only from retrieved sources; when no source is relevant the service returns an explicit refusal instead of guessing.

Every response that can fail includes an error code suitable for programmatic handling. The frontend maps those codes to inline messages, retry actions, and toasts. Logs record the failing operation and document identifier without logging API keys or full document text.

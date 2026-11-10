# How HNSWhy works

HNSWhy is a retrieval-augmented question answering application built around a from-scratch vector search engine. A user creates a knowledge base by uploading PDF, Markdown, or plain-text documents. The server extracts text, splits it into overlapping word-boundary chunks, embeds each chunk, and stores the result in SQLite. The search index is an in-memory HNSW graph that references those stored vectors.

## Ingestion

Uploads are accepted as multipart files or as JSON text. PDF extraction uses `pdftotext`, while text and Markdown are read directly. The chunker targets roughly 350 words with a 50-word overlap by default, which keeps each chunk small enough to fit in a prompt while preserving context across boundaries. Stable document and chunk identifiers are assigned by SQLite autoincrement columns.

Embedding providers are pluggable. The offline default is a deterministic feature-hashing embedder that needs no network access. Ollama and OpenAI-compatible HTTP providers are supported for higher-quality embeddings. The active provider, model, and dimension are recorded in the database metadata. HNSWhy refuses to mix incompatible embedding spaces and offers a re-embedding mode that rebuilds every vector with the active provider.

## Consistency

SQLite is the source of truth. Documents, chunks, embeddings, and metadata all live there. The vector index is rebuilt from SQLite at startup, and new documents are added incrementally without rebuilding the whole graph. A document deletion removes its rows with a cascading foreign key and marks the corresponding index slots as tombstones. If an incremental graph update fails, the server logs the failure and rebuilds the index from SQLite. One application mutex serialises uploads, deletes, and searches so concurrent HTTP requests cannot corrupt shared state.

## Question answering

A question is embedded with the same provider used for the corpus. The query vector is searched against the HNSW graph, and the retrieved chunk identifiers are used to load the original text from SQLite. Those chunks become numbered sources. The chat provider receives a grounded prompt that instructs it to answer only from the sources and cite them as `[1]`, `[2]`, and so on. If no relevant source exists, the service returns an explicit refusal instead of inventing an answer.

The offline extractive provider is a deterministic fallback. It ranks sentences by term overlap with the question and returns the best sentences with citations. This keeps the whole demo usable without Ollama or an API key. The Ollama and OpenAI-compatible providers produce more natural answers, but the retrieval and citation contract is the same.

## API and interface

The HTTP API exposes health, status, document listing and upload, document detail and deletion, question answering, a map of sampled embeddings, and the benchmark report. Errors use a consistent `{"error": {"code": "...", "message": "..."}}` shape. The frontend is a separate application that talks to these endpoints; in production its built assets are served by the same C++ server.

The map endpoint returns a bounded random sample of chunk vectors. The frontend projects those vectors to two and three dimensions for exploration. The projection is a visual aid, not a picture of the HNSW graph: it helps users inspect clusters and individual chunks, while the graph itself remains an internal search structure.

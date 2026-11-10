# Embeddings and provider compatibility

An embedding turns a chunk of text into a fixed-length vector. HNSWhy uses the same embedder for corpus chunks and for incoming questions, so a query lands in the same coordinate space as the documents it searches. The active provider, model, and dimension are stored in SQLite metadata and returned by the status endpoint.

## Providers

The default provider is a deterministic feature-hashing embedder. It maps tokens into a 256-dimensional vector with signed hashing, normalizes the result, and requires no network access. This makes the sample library, tests, and offline demo work out of the box. The hash embedder is a lexical baseline: it captures shared words rather than deep semantics.

Ollama embeddings are supported through a local base URL. A typical configuration uses `nomic-embed-text`, and the dimension is discovered from the first response unless it is set explicitly. OpenAI-compatible embedding endpoints are also supported, including hosted APIs and local gateways. The server sends the configured model name and records the returned vectors exactly as they arrive.

Provider configuration is independent between embeddings and chat. A deployment can embed locally with Ollama and answer with a hosted chat model, or use a hosted embedder with the offline extractive answerer. API keys are read from the environment and never written to the database, logs, or process arguments.

## Dimensions and compatibility

Vectors from different models are not comparable, even when their dimensions happen to match. A 384-dimensional vector from one sentence encoder and a 384-dimensional vector from another live in unrelated coordinate systems. HNSWhy treats the embedding space as part of the database identity, not as an implementation detail.

When the configured provider, model, or dimension differs from the stored metadata, the server refuses to mix the two spaces. The safe path is an explicit re-embedding run: every chunk is loaded from SQLite, embedded again with the new provider, and written back together with the new metadata. The in-memory HNSW graph is then rebuilt from the updated vectors. No document text is lost, and no stale vectors remain in the index.

## Query path

At query time the question is embedded once. The resulting vector is validated against the index dimension, then searched through the HNSW graph. Retrieved slot identifiers map back to chunk identifiers, and the chunk text is loaded from SQLite for the prompt. The original query vector is also returned by the answer endpoint so the map can display where the question sits relative to the corpus.

Embedding failures are surfaced as provider errors with a stable error code. A timed-out or malformed response never inserts a partial vector. If an upload fails after the document row is written, the transaction is rolled back and the index is left unchanged.

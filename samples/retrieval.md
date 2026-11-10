# Retrieval-augmented generation

Retrieval-augmented generation, or RAG, combines a search system with a language model. The retriever selects relevant passages from a knowledge base, and the model writes an answer conditioned on those passages. This grounding reduces hallucinations and lets the model answer questions about private documents that were never part of its training data.

## The pipeline

The ingestion pipeline extracts text, chunks it, embeds each chunk, and stores the vectors and text. The query pipeline embeds the question, searches for the nearest chunks, loads their text, and builds a prompt. A good prompt tells the model to use only the supplied sources, to cite each source, and to refuse when the answer is absent. Citations make the answer auditable: a reader can jump from a claim to the chunk that supports it.

Chunking is a design choice with real consequences. Chunks that are too large dilute the embedding and waste prompt budget. Chunks that are too small lose context and split a fact across boundaries. Overlapping windows are a practical compromise. HNSWhy uses word-boundary windows with a configurable target and overlap, and caps the number of chunks per document to protect the server from pathological uploads.

## Retrieval quality

Retrieval quality and answer quality are separate concerns. A retriever can return the right chunk while the language model still writes a poor answer, or a model can sound convincing while the retriever returned an irrelevant passage. HNSWhy evaluates them separately. The offline benchmark measures HNSW Recall@1, Recall@10, QPS, and latency against exact ground truth. The application exposes the retrieved sources and their distances alongside every answer so the retrieval step can be inspected directly.

The embedding space matters. Vectors from different providers or models are not comparable, even when their dimensions happen to match. Mixing them silently produces meaningless distances. HNSWhy records provider, model, and dimension in SQLite and refuses to start with an incompatible combination unless the user explicitly asks for re-embedding. Re-embedding rewrites every stored vector and rebuilds the index.

## Grounding and refusal

A grounded assistant should say when the documents do not contain an answer. HNSWhy uses an exact refusal sentence when the index is empty or when the offline extractive provider finds no lexical overlap. The prompt for external chat providers repeats the same instruction. This is intentionally conservative: a clear refusal is more useful than a plausible fabrication.

## Limitations

RAG is not a substitute for careful data curation. Scanned PDFs without an OCR layer cannot be extracted by `pdftotext`. Very large documents are capped, and the default chunker is word-based rather than semantic. The hash embedding provider is designed for offline demos and deterministic tests, not for production semantic search. For production quality, configure a real embedding model and a chat model, then re-embed the corpus so every vector lives in the same space.

# SQLite as the source of truth

HNSWhy stores documents, chunks, embeddings, and metadata in a single SQLite database. The in-memory vector index is a derived acceleration structure: it is rebuilt from SQLite at startup and can be discarded at any time without losing user data. This separation keeps persistence simple and makes recovery predictable.

## Schema

The `documents` table holds the title, source, full extracted text, and creation timestamp. The `chunks` table stores one row per chunk with a foreign key to its document, an ordinal, and the chunk text. The `embeddings` table stores the float vector for each chunk as a binary blob, with a cascade delete back to the chunk. A small `metadata` table records the embedding provider, model, dimension, sample status, and schema version.

Identifiers come from SQLite autoincrement columns and never change. Chunk identifiers are stable across restarts and re-embedding, which lets the runtime index and the API refer to the same rows without a translation table. Foreign keys are enabled on every connection, and deleting a document cascades to its chunks and embeddings in one transaction.

## Connections and durability

The server opens SQLite in serialized mode with WAL journaling. WAL allows readers and writers to overlap without blocking the whole database, while the application mutex still serializes index mutations. Busy timeouts and foreign-key enforcement are set explicitly on every connection so behavior does not depend on driver defaults.

Writes use prepared statements and explicit transactions. A failed insert rolls back entirely, including the document row. The application checks the schema version on startup and refuses to run against an unknown future version rather than silently corrupting data.

## Startup and recovery

Startup opens the database, verifies the embedding metadata, loads every embedded chunk in identifier order, and inserts the vectors into a fresh HNSW graph. The slot map is rebuilt alongside the graph. If the database is empty and sample loading is enabled, the sample Markdown files are ingested once and the `sample_loaded` marker is set.

The server never seeds over an existing library. Once any document exists, the marker or the document count prevents sample ingestion. A restart reuses the same rows and identifiers; it does not duplicate chunks or reset user uploads.

If the database and the index disagree, SQLite wins. A failed incremental insertion triggers a guarded rebuild from the stored rows. The same path is used when the server starts after an unclean shutdown, because the graph is never assumed to have been persisted.

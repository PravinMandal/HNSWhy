#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hnswhy {

struct TextChunk {
    std::string text;
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct ChunkOptions {
    std::size_t target_words = 350;
    std::size_t overlap_words = 50;
    std::size_t max_chunks = 2000;
};

// Splits normalized text on word boundaries. Chunks overlap by `overlap_words`
// and keep the original spacing inside each window.
std::vector<TextChunk> chunk_text(std::string_view text, const ChunkOptions& options = {});

std::string normalize_newlines(std::string_view text);
std::string trim_copy(std::string_view text);

// Extracts UTF-8 text from an upload. Supports .txt, .md/.markdown and .pdf.
// PDFs are delegated to `pdftotext`; a scanned PDF with no text layer fails.
std::string extract_text(std::string_view bytes, std::string_view filename,
                         std::string_view content_type);

}  // namespace hnswhy

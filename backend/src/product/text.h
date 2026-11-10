#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hnswhy {

// True when text is well-formed UTF-8. API input, SQLite text, and provider
// output all cross a strict JSON boundary, so malformed bytes must be rejected
// or replaced before serialization.
bool is_valid_utf8(std::string_view text);

// Number of Unicode code points in valid UTF-8 text.
std::size_t utf8_length(std::string_view text);

// Cuts at a UTF-8 codepoint boundary. The result is at most max_bytes long and
// never ends in the middle of a multi-byte sequence.
std::string truncate_utf8(std::string_view text, std::size_t max_bytes);

// truncate_utf8 plus a best-effort back-off to the last ASCII space. Used for
// human-readable previews and prompt excerpts.
std::string truncate_utf8_words(std::string_view text, std::size_t max_bytes);

// ASCII-aware tokenization. Bytes >= 0x80 are kept inside a token so UTF-8
// words are not split into individual bytes.
std::vector<std::string> tokenize_lower(std::string_view text);
std::vector<std::string> content_terms(std::string_view text);
std::vector<std::string> split_sentences(std::string_view text);

bool finite_embedding(std::span<const float> values);
std::vector<float> normalize_embedding(std::vector<float> values);

}  // namespace hnswhy

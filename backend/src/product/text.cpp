#include "product/text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace hnswhy {
namespace {

bool is_word_byte(unsigned char c) {
    return std::isalnum(c) != 0 || c >= 0x80;
}

char lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> words = {
        "a",      "an",     "and",     "are",     "as",      "at",     "be",
        "been",   "but",    "by",      "can",     "could",   "did",    "do",
        "does",   "for",    "from",    "had",     "has",     "have",   "how",
        "i",      "if",     "in",      "into",    "is",      "it",     "its",
        "me",     "my",     "of",      "on",      "or",      "our",    "ours",
        "please", "so",     "than",    "that",    "the",     "their",  "them",
        "then",   "there",  "these",   "they",    "this",    "those",  "to",
        "was",    "we",     "were",    "what",    "when",    "where",  "which",
        "who",    "why",    "will",    "with",    "would",   "you",    "your",
    };
    return words;
}

bool is_continuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

}  // namespace

bool is_valid_utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80U) {
            ++i;
            continue;
        }

        std::size_t extra = 0;
        unsigned int codepoint = 0;
        if ((lead & 0xE0U) == 0xC0U) {
            extra = 1;
            codepoint = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            extra = 2;
            codepoint = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            extra = 3;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (extra > text.size() - i - 1) {
            return false;
        }
        for (std::size_t offset = 1; offset <= extra; ++offset) {
            const unsigned char byte = static_cast<unsigned char>(text[i + offset]);
            if (!is_continuation(byte)) {
                return false;
            }
            codepoint = (codepoint << 6U) | (byte & 0x3FU);
        }

        // Reject overlong encodings, UTF-16 surrogates, and values outside the
        // Unicode range. These byte sequences are not valid UTF-8 even though
        // they have the right shape.
        if ((extra == 1 && codepoint < 0x80U) || (extra == 2 && codepoint < 0x800U) ||
            (extra == 3 && codepoint < 0x10000U) || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

std::size_t utf8_length(std::string_view text) {
    std::size_t count = 0;
    for (const char byte : text) {
        if (!is_continuation(static_cast<unsigned char>(byte))) {
            ++count;
        }
    }
    return count;
}

std::string truncate_utf8(std::string_view text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return std::string(text);
    }
    std::size_t end = max_bytes;
    while (end > 0 && is_continuation(static_cast<unsigned char>(text[end]))) {
        --end;
    }
    return std::string(text.substr(0, end));
}

std::string truncate_utf8_words(std::string_view text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return std::string(text);
    }
    std::string prefix = truncate_utf8(text, max_bytes);
    const std::size_t space = prefix.rfind(' ');
    if (space != std::string::npos && space >= max_bytes / 2) {
        prefix.resize(space);
    }
    while (!prefix.empty() &&
           std::isspace(static_cast<unsigned char>(prefix.back())) != 0) {
        prefix.pop_back();
    }
    prefix += "...";
    return prefix;
}

std::vector<std::string> tokenize_lower(std::string_view text) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() &&
               !is_word_byte(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i == text.size()) {
            break;
        }
        const std::size_t begin = i;
        while (i < text.size() && is_word_byte(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        std::string token(text.substr(begin, i - begin));
        std::transform(token.begin(), token.end(), token.begin(), lower_ascii);
        tokens.push_back(std::move(token));
    }
    return tokens;
}

std::vector<std::string> content_terms(std::string_view text) {
    std::vector<std::string> all = tokenize_lower(text);
    std::vector<std::string> filtered;
    filtered.reserve(all.size());
    for (std::string& token : all) {
        if (token.size() >= 2 && stopwords().count(token) == 0) {
            filtered.push_back(std::move(token));
        }
    }
    if (filtered.empty()) {
        return all;
    }
    return filtered;
}

std::vector<std::string> split_sentences(std::string_view text) {
    std::vector<std::string> sentences;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool terminator = c == '.' || c == '?' || c == '!' || c == '\n';
        if (!terminator) {
            continue;
        }
        std::string sentence = trim(text.substr(begin, i + (c == '\n' ? 0 : 1) - begin));
        if (sentence.size() >= 8) {
            sentences.push_back(std::move(sentence));
        }
        begin = i + 1;
    }
    if (begin < text.size()) {
        std::string sentence = trim(text.substr(begin));
        if (sentence.size() >= 8) {
            sentences.push_back(std::move(sentence));
        }
    }
    return sentences;
}

bool finite_embedding(std::span<const float> values) {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

std::vector<float> normalize_embedding(std::vector<float> values) {
    double sum = 0.0;
    for (const float value : values) {
        sum += static_cast<double>(value) * static_cast<double>(value);
    }
    if (sum <= 0.0 || !std::isfinite(sum)) {
        return values;
    }
    const double norm = std::sqrt(sum);
    for (float& value : values) {
        value = static_cast<float>(static_cast<double>(value) / norm);
    }
    return values;
}

}  // namespace hnswhy

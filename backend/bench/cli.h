#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace hnswhy::bench {

// Parses a non-negative decimal size.
//
// std::stoull on its own accepts "-1" (wrapping to SIZE_MAX) and "+5", so a
// typo like "--passes -1" would turn into a near-infinite loop. Reject every
// non-digit before converting; overflow still throws std::out_of_range.
inline std::size_t parse_size(const std::string& text, const char* flag) {
    if (text.empty() ||
        !std::all_of(text.begin(), text.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        throw std::invalid_argument(std::string("invalid value for ") + flag + ": " + text);
    }
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
        throw std::invalid_argument(std::string("invalid value for ") + flag + ": " + text);
    }
    return static_cast<std::size_t>(value);
}

// Parses a comma-separated efSearch grid. Values are sorted and deduplicated so
// a repeated label cannot produce two rows that claim to measure the same run.
// Zero is rejected: the engine clamps ef up to k, so "ef=0" would really be
// "ef=k" and the report would lie.
inline std::vector<std::size_t> parse_ef_list(const std::string& text) {
    std::vector<std::size_t> values;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string token =
            comma == std::string::npos ? text.substr(start) : text.substr(start, comma - start);
        if (token.empty()) {
            throw std::invalid_argument("empty efSearch value");
        }
        const std::size_t value = parse_size(token, "--ef");
        if (value == 0) {
            throw std::invalid_argument("efSearch must be positive");
        }
        values.push_back(value);
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

}  // namespace hnswhy::bench

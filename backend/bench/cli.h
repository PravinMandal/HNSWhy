#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>

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

}  // namespace hnswhy::bench

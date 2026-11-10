#pragma once

#include <stdexcept>
#include <string>

namespace hnswhy {

// Error carrying an API-safe code and HTTP status. Product code throws this
// instead of leaking SQLite or HTTP details into route handlers.
class ProductError : public std::runtime_error {
public:
    ProductError(std::string code, std::string message, int status)
        : std::runtime_error(std::move(message)), code_(std::move(code)), status_(status) {}

    const std::string& code() const noexcept { return code_; }
    int status() const noexcept { return status_; }

private:
    std::string code_;
    int status_;
};

inline ProductError validation_error(std::string message) {
    return ProductError("INVALID_REQUEST", std::move(message), 400);
}

inline ProductError not_found_error(std::string message) {
    return ProductError("NOT_FOUND", std::move(message), 404);
}

inline ProductError provider_error(std::string code, std::string message) {
    return ProductError(std::move(code), std::move(message), 503);
}

}  // namespace hnswhy

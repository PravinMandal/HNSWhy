#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hnswhy {

struct HttpEndpoint {
    std::string origin;  // scheme://host[:port]
    std::string prefix;  // path from the base URL, without a trailing slash
    bool secure = false;
};

struct HttpResult {
    int status = 0;
    std::string body;
};

// Accepts http(s)://host[:port][/path] and rejects query strings/fragments.
HttpEndpoint parse_endpoint(std::string_view base_url);

HttpResult http_get(const HttpEndpoint& endpoint, std::string_view path,
                    const std::vector<std::pair<std::string, std::string>>& headers,
                    int timeout_seconds);

HttpResult http_post(const HttpEndpoint& endpoint, std::string_view path,
                     const std::string& body,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     int timeout_seconds);

std::string short_body(std::string_view body, std::size_t limit = 400);

}  // namespace hnswhy

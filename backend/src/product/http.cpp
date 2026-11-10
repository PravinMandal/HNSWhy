#include "product/http.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "httplib.h"
#include "product/errors.h"
#include "product/text.h"

namespace hnswhy {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string join_path(const HttpEndpoint& endpoint, std::string_view path) {
    std::string result = endpoint.prefix;
    if (!path.empty() && path.front() != '/') {
        result.push_back('/');
    }
    result += path;
    return result;
}

httplib::Headers to_headers(
    const std::vector<std::pair<std::string, std::string>>& headers) {
    httplib::Headers result;
    for (const auto& [name, value] : headers) {
        result.emplace(name, value);
    }
    return result;
}

}  // namespace

HttpEndpoint parse_endpoint(std::string_view base_url) {
    std::size_t begin = 0;
    while (begin < base_url.size() &&
           std::isspace(static_cast<unsigned char>(base_url[begin])) != 0) {
        ++begin;
    }
    std::size_t end = base_url.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(base_url[end - 1])) != 0) {
        --end;
    }
    const std::string url(base_url.substr(begin, end - begin));

    const std::size_t scheme_separator = url.find("://");
    if (scheme_separator == std::string::npos) {
        throw ProductError("PROVIDER_CONFIG",
                           "provider base URL must start with http:// or https://", 500);
    }

    const std::string scheme = lower(url.substr(0, scheme_separator));
    if (scheme != "http" && scheme != "https") {
        throw ProductError("PROVIDER_CONFIG",
                           "provider base URL must use http:// or https://", 500);
    }

    const std::size_t authority_begin = scheme_separator + 3;
    const std::size_t path_begin = url.find('/', authority_begin);
    const std::string authority =
        url.substr(authority_begin, path_begin == std::string::npos
                                         ? std::string::npos
                                         : path_begin - authority_begin);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        url.find('?') != std::string::npos || url.find('#') != std::string::npos) {
        throw ProductError("PROVIDER_CONFIG", "provider base URL is invalid", 500);
    }

    HttpEndpoint endpoint;
    endpoint.origin = scheme + "://" + authority;
    endpoint.secure = scheme == "https";
    if (path_begin != std::string::npos) {
        endpoint.prefix = url.substr(path_begin);
        while (endpoint.prefix.size() > 1 && endpoint.prefix.back() == '/') {
            endpoint.prefix.pop_back();
        }
        if (endpoint.prefix == "/") {
            endpoint.prefix.clear();
        }
    }
    return endpoint;
}

HttpResult http_get(const HttpEndpoint& endpoint, std::string_view path,
                    const std::vector<std::pair<std::string, std::string>>& headers,
                    int timeout_seconds) {
    if (timeout_seconds < 1) {
        throw ProductError("PROVIDER_CONFIG", "provider timeout must be positive", 500);
    }
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    if (endpoint.secure) {
        throw ProductError("HTTPS_UNSUPPORTED",
                           "this build has no HTTPS support; rebuild with OpenSSL installed",
                           500);
    }
#endif
    httplib::Client client(endpoint.origin);
    client.set_connection_timeout(timeout_seconds, 0);
    client.set_read_timeout(timeout_seconds, 0);
    client.set_write_timeout(timeout_seconds, 0);
    client.set_follow_location(true);

    const auto response = client.Get(join_path(endpoint, path).c_str(), to_headers(headers));
    if (!response) {
        throw ProductError("PROVIDER_UNAVAILABLE",
                           "cannot reach provider at " + endpoint.origin, 503);
    }
    return HttpResult{response->status, response->body};
}

HttpResult http_post(const HttpEndpoint& endpoint, std::string_view path,
                     const std::string& body,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     int timeout_seconds) {
    if (timeout_seconds < 1) {
        throw ProductError("PROVIDER_CONFIG", "provider timeout must be positive", 500);
    }
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    if (endpoint.secure) {
        throw ProductError("HTTPS_UNSUPPORTED",
                           "this build has no HTTPS support; rebuild with OpenSSL installed",
                           500);
    }
#endif
    httplib::Client client(endpoint.origin);
    client.set_connection_timeout(timeout_seconds, 0);
    client.set_read_timeout(timeout_seconds, 0);
    client.set_write_timeout(timeout_seconds, 0);
    client.set_follow_location(true);

    const auto response =
        client.Post(join_path(endpoint, path).c_str(), to_headers(headers), body,
                    "application/json");
    if (!response) {
        throw ProductError("PROVIDER_UNAVAILABLE",
                           "cannot reach provider at " + endpoint.origin, 503);
    }
    return HttpResult{response->status, response->body};
}

std::string short_body(std::string_view body, std::size_t limit) {
    return truncate_utf8_words(body, limit);
}

}  // namespace hnswhy

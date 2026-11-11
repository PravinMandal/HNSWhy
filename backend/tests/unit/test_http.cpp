#include "product/http.h"

#include <string>
#include <vector>

#include "framework/test_framework.h"
#include "product/errors.h"

namespace {

// Returns the product error code, or "no error" when parsing succeeds. Keeping
// the invalid cases in one helper avoids repeating the try/catch eight times.
std::string parse_error_code(const std::string& url) {
    try {
        (void)hnswhy::parse_endpoint(url);
    } catch (const hnswhy::ProductError& error) {
        return error.code();
    }
    return "no error";
}

}  // namespace

HNSWHY_TEST(http_parse_endpoint_normalizes_and_validates) {
    const hnswhy::HttpEndpoint trimmed = hnswhy::parse_endpoint(" http://host/v1/ ");
    CHECK_EQ(trimmed.origin, std::string("http://host"));
    CHECK_EQ(trimmed.prefix, std::string("/v1"));
    CHECK(!trimmed.secure);

    const hnswhy::HttpEndpoint secure = hnswhy::parse_endpoint("https://host/");
    CHECK_EQ(secure.origin, std::string("https://host"));
    CHECK_EQ(secure.prefix, std::string());
    CHECK(secure.secure);

    const hnswhy::HttpEndpoint plain = hnswhy::parse_endpoint("http://127.0.0.1:11434");
    CHECK_EQ(plain.origin, std::string("http://127.0.0.1:11434"));
    CHECK_EQ(plain.prefix, std::string());

    CHECK_EQ(parse_error_code("host:11434"), std::string("PROVIDER_CONFIG"));
    CHECK_EQ(parse_error_code("ftp://host"), std::string("PROVIDER_CONFIG"));
    CHECK_EQ(parse_error_code("http://user:pass@host/v1"), std::string("PROVIDER_CONFIG"));
    CHECK_EQ(parse_error_code("http://host/v1?x=1"), std::string("PROVIDER_CONFIG"));
    CHECK_EQ(parse_error_code("http://host/v1#f"), std::string("PROVIDER_CONFIG"));
    CHECK_EQ(parse_error_code("http:///v1"), std::string("PROVIDER_CONFIG"));
}

HNSWHY_TEST(http_rejects_non_positive_timeouts) {
    // The timeout check runs before any socket is opened, so this needs no
    // server and cannot hang on a refused connection.
    const hnswhy::HttpEndpoint endpoint{"http://127.0.0.1:9", "", false};
    CHECK_THROWS_AS(hnswhy::http_get(endpoint, "/", {}, 0), hnswhy::ProductError);
    CHECK_THROWS_AS(hnswhy::http_post(endpoint, "/", "{}", {}, 0), hnswhy::ProductError);
    CHECK_THROWS_AS(hnswhy::http_get(endpoint, "/", {}, -1), hnswhy::ProductError);
}

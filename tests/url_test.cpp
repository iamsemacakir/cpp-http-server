#include <string>
#include <utility>
#include <vector>

#include "http_request.hpp"
#include "url.hpp"
#include "test_framework.hpp"

using http::parse_query;
using http::QueryParams;
using http::url_decode;
using http::url_encode;

namespace {

// Flattens query parameters into "k=v" strings, in key order, so whole
// results can be compared with a single CHECK_EQ.
std::string flatten(const QueryParams& params) {
    std::string result;
    for (const auto& [key, value] : params) {
        if (!result.empty()) {
            result += '&';
        }
        result += key + '=' + value;
    }
    return result;
}

} // namespace

// --- url_decode --------------------------------------------------------------

TEST_CASE("url_decode leaves plain text untouched") {
    CHECK_EQ(url_decode(""), "");
    CHECK_EQ(url_decode("/index.html"), "/index.html");
    CHECK_EQ(url_decode("a-b_c.d~e"), "a-b_c.d~e");
}

TEST_CASE("url_decode decodes percent-escapes in either case") {
    CHECK_EQ(url_decode("hello%20world"), "hello world");
    CHECK_EQ(url_decode("%2Fetc%2fpasswd"), "/etc/passwd");
    CHECK_EQ(url_decode("100%25"), "100%");
    CHECK_EQ(url_decode("%41%42%43"), "ABC");
}

TEST_CASE("url_decode decodes multi-byte UTF-8 sequences") {
    CHECK_EQ(url_decode("caf%C3%A9"), "caf\xC3\xA9");
    CHECK_EQ(url_decode("%C5%9Fehir"), "\xC5\x9F" "ehir");
}

TEST_CASE("url_decode keeps a decoded NUL byte") {
    const auto decoded = url_decode("a%00b");
    REQUIRE(decoded.has_value());
    CHECK_EQ(decoded->size(), std::size_t{3});
    CHECK_EQ((*decoded)[1], '\0');
}

TEST_CASE("url_decode treats '+' as a space only when asked") {
    CHECK_EQ(url_decode("a+b"), "a+b");
    CHECK_EQ(url_decode("a+b", true), "a b");
    CHECK_EQ(url_decode("a%2Bb", true), "a+b");
}

TEST_CASE("url_decode rejects malformed escapes") {
    CHECK_EQ(url_decode("%"), std::nullopt);
    CHECK_EQ(url_decode("%2"), std::nullopt);
    CHECK_EQ(url_decode("abc%"), std::nullopt);
    CHECK_EQ(url_decode("%G1"), std::nullopt);
    CHECK_EQ(url_decode("%1G"), std::nullopt);
    CHECK_EQ(url_decode("%%41"), std::nullopt);
    CHECK_EQ(url_decode("ok%2"), std::nullopt);
}

// --- url_encode --------------------------------------------------------------

TEST_CASE("url_encode keeps unreserved characters") {
    CHECK_EQ(url_encode("AZaz09-._~"), "AZaz09-._~");
    CHECK_EQ(url_encode(""), "");
}

TEST_CASE("url_encode escapes reserved and non-ASCII bytes in upper case") {
    CHECK_EQ(url_encode("a b"), "a%20b");
    CHECK_EQ(url_encode("/?#&=+%"), "%2F%3F%23%26%3D%2B%25");
    CHECK_EQ(url_encode("caf\xC3\xA9"), "caf%C3%A9");
    CHECK_EQ(url_encode(std::string("\0", 1)), "%00");
}

TEST_CASE("url_encode and url_decode round-trip arbitrary bytes") {
    std::string bytes;
    for (int c = 0; c < 256; ++c) {
        bytes += static_cast<char>(c);
    }
    const std::string encoded = url_encode(bytes);
    CHECK_EQ(url_decode(encoded), bytes);
    CHECK_EQ(url_decode(encoded, true), bytes); // '+' is always escaped
}

// --- parse_query -------------------------------------------------------------

TEST_CASE("parse_query handles empty input") {
    const auto params = parse_query("");
    REQUIRE(params.has_value());
    CHECK(params->empty());
}

TEST_CASE("parse_query splits pairs and decodes keys and values") {
    const auto params = parse_query("q=hello+world&lang=c%2B%2B&page=2");
    REQUIRE(params.has_value());
    CHECK_EQ(flatten(*params), "lang=c++&page=2&q=hello world");
}

TEST_CASE("parse_query keeps flags and empty values") {
    const auto params = parse_query("debug&name=&x=1");
    REQUIRE(params.has_value());
    CHECK_EQ(params->size(), std::size_t{3});
    CHECK_EQ(params->find("debug")->second, "");
    CHECK_EQ(params->find("name")->second, "");
}

TEST_CASE("parse_query skips empty pairs") {
    const auto params = parse_query("&a=1&&b=2&");
    REQUIRE(params.has_value());
    CHECK_EQ(flatten(*params), "a=1&b=2");
}

TEST_CASE("parse_query keeps repeated keys in input order") {
    const auto params = parse_query("tag=b&tag=a&tag=c");
    REQUIRE(params.has_value());
    CHECK_EQ(params->count("tag"), std::size_t{3});

    std::vector<std::string> tags;
    const auto [first, last] = params->equal_range("tag");
    for (auto it = first; it != last; ++it) {
        tags.push_back(it->second);
    }
    CHECK(tags == (std::vector<std::string>{"b", "a", "c"}));
}

TEST_CASE("parse_query splits on the first '=' only") {
    const auto params = parse_query("expr=a=b&eq==");
    REQUIRE(params.has_value());
    CHECK_EQ(params->find("expr")->second, "a=b");
    CHECK_EQ(params->find("eq")->second, "=");
}

TEST_CASE("parse_query rejects malformed escapes in keys or values") {
    CHECK_EQ(parse_query("a=%ZZ").has_value(), false);
    CHECK_EQ(parse_query("%2=1").has_value(), false);
    CHECK_EQ(parse_query("ok=1&bad=%").has_value(), false);
}

// --- Integration with the request parser -------------------------------------

TEST_CASE("parsed requests expose decoded query parameters") {
    const auto request = http::parse_request_line("GET /search?q=a%26b+c&x=1&x=2 HTTP/1.1");
    CHECK_EQ(request.query, "q=a%26b+c&x=1&x=2");
    CHECK_EQ(request.query_param("q"), "a&b c");
    CHECK_EQ(request.query_param("x"), "1"); // first value of a repeated key
    CHECK_EQ(request.query_param("missing"), std::nullopt);
}

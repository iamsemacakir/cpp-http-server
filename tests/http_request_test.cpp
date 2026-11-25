#include <sys/socket.h>

#include <string>
#include <system_error>
#include <utility>

#include "http_request.hpp"
#include "request_reader.hpp"
#include "socket.hpp"
#include "test_framework.hpp"

using http::HttpRequest;
using http::Method;
using http::ParseError;
using http::parse_request_head;
using http::parse_request_line;

namespace {

// A connected pair of local sockets: bytes written to `client` are read by
// a RequestReader on `server`, without touching the network.
struct SocketPair {
    http::Socket client;
    http::Socket server;
};

SocketPair make_socket_pair() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        throw std::system_error(errno, std::generic_category(), "socketpair");
    }
    return SocketPair{http::Socket(fds[0]), http::Socket(fds[1])};
}

} // namespace

// --- Methods -----------------------------------------------------------------

TEST_CASE("method_from_string maps every supported method") {
    CHECK(http::method_from_string("GET") == Method::Get);
    CHECK(http::method_from_string("HEAD") == Method::Head);
    CHECK(http::method_from_string("POST") == Method::Post);
    CHECK(http::method_from_string("PUT") == Method::Put);
    CHECK(http::method_from_string("PATCH") == Method::Patch);
    CHECK(http::method_from_string("DELETE") == Method::Delete);
    CHECK(http::method_from_string("OPTIONS") == Method::Options);
}

TEST_CASE("method names are case-sensitive and unknown ones are Unknown") {
    CHECK(http::method_from_string("get") == Method::Unknown);
    CHECK(http::method_from_string("BREW") == Method::Unknown);
    CHECK(http::method_from_string("") == Method::Unknown);
}

TEST_CASE("to_string round-trips methods") {
    CHECK_EQ(http::to_string(Method::Get), "GET");
    CHECK_EQ(http::to_string(Method::Delete), "DELETE");
    CHECK_EQ(http::to_string(Method::Unknown), "UNKNOWN");
}

// --- Request line ------------------------------------------------------------

TEST_CASE("parses a simple request line") {
    const HttpRequest request = parse_request_line("GET /index.html HTTP/1.1");
    CHECK(request.method == Method::Get);
    CHECK_EQ(request.target, "/index.html");
    CHECK_EQ(request.path, "/index.html");
    CHECK_EQ(request.query, "");
    CHECK(request.query_params.empty());
    CHECK_EQ(request.version, "HTTP/1.1");
}

TEST_CASE("accepts HTTP/1.0 and unknown methods") {
    const HttpRequest request = parse_request_line("BREW /pot HTTP/1.0");
    CHECK(request.method == Method::Unknown);
    CHECK_EQ(request.version, "HTTP/1.0");
}

TEST_CASE("splits the query string from the path") {
    const HttpRequest request = parse_request_line("GET /search?q=hello+world&page=2 HTTP/1.1");
    CHECK_EQ(request.target, "/search?q=hello+world&page=2");
    CHECK_EQ(request.path, "/search");
    CHECK_EQ(request.query, "q=hello+world&page=2");
    CHECK_EQ(request.query_param("q"), "hello world");
    CHECK_EQ(request.query_param("page"), "2");
    CHECK_EQ(request.query_param("missing"), std::nullopt);
}

TEST_CASE("percent-decodes the path but keeps '+' literal") {
    const HttpRequest request = parse_request_line("GET /my%20files/a+b.txt HTTP/1.1");
    CHECK_EQ(request.path, "/my files/a+b.txt");
    CHECK_EQ(request.target, "/my%20files/a+b.txt");
}

TEST_CASE("accepts the asterisk target") {
    const HttpRequest request = parse_request_line("OPTIONS * HTTP/1.1");
    CHECK(request.method == Method::Options);
    CHECK_EQ(request.target, "*");
}

TEST_CASE("rejects request lines without three parts") {
    CHECK_THROWS_AS(parse_request_line(""), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET /"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET / HTTP/1.1 extra"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET  / HTTP/1.1"), ParseError);
    CHECK_THROWS_AS(parse_request_line(" / HTTP/1.1"), ParseError);
}

TEST_CASE("rejects targets that are not origin-form") {
    CHECK_THROWS_AS(parse_request_line("GET index.html HTTP/1.1"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET http://example.com/ HTTP/1.1"), ParseError);
}

TEST_CASE("rejects unsupported HTTP versions") {
    CHECK_THROWS_AS(parse_request_line("GET / HTTP/2.0"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET / HTTP/0.9"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET / http/1.1"), ParseError);
}

TEST_CASE("rejects malformed percent-encoding and NUL bytes") {
    CHECK_THROWS_AS(parse_request_line("GET /bad%G1 HTTP/1.1"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET /truncated%2 HTTP/1.1"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET /nul%00byte HTTP/1.1"), ParseError);
    CHECK_THROWS_AS(parse_request_line("GET /search?q=%ZZ HTTP/1.1"), ParseError);
}

// --- Headers -----------------------------------------------------------------

TEST_CASE("parses headers with case-insensitive lookup") {
    const HttpRequest request = parse_request_head(
        "GET / HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: test-suite/1.0\r\n"
        "Accept: */*");
    CHECK_EQ(request.headers.size(), std::size_t{3});
    CHECK_EQ(request.header("Host"), "localhost:8080");
    CHECK_EQ(request.header("host"), "localhost:8080");
    CHECK_EQ(request.header("USER-AGENT"), "test-suite/1.0");
    CHECK_EQ(request.header("X-Missing"), std::nullopt);
}

TEST_CASE("trims whitespace around header values") {
    const HttpRequest request = parse_request_head(
        "GET / HTTP/1.1\r\n"
        "X-Padded: \t  value with spaces \t\r\n"
        "X-Empty:");
    CHECK_EQ(request.header("X-Padded"), "value with spaces");
    CHECK_EQ(request.header("X-Empty"), "");
}

TEST_CASE("combines repeated headers into a list") {
    const HttpRequest request = parse_request_head(
        "GET / HTTP/1.1\r\n"
        "Accept: text/html\r\n"
        "accept: application/json");
    CHECK_EQ(request.header("Accept"), "text/html, application/json");
}

TEST_CASE("keeps colons inside header values") {
    const HttpRequest request = parse_request_head(
        "GET / HTTP/1.1\r\n"
        "Referer: http://localhost:8080/a");
    CHECK_EQ(request.header("Referer"), "http://localhost:8080/a");
}

TEST_CASE("rejects malformed header lines") {
    CHECK_THROWS_AS(parse_request_head("GET / HTTP/1.1\r\nNoColonHere"), ParseError);
    CHECK_THROWS_AS(parse_request_head("GET / HTTP/1.1\r\n: no-name"), ParseError);
    CHECK_THROWS_AS(parse_request_head("GET / HTTP/1.1\r\nBad Name: x"), ParseError);
    CHECK_THROWS_AS(parse_request_head("GET / HTTP/1.1\r\nBad(Name): x"), ParseError);
}

TEST_CASE("request line errors propagate from parse_request_head") {
    CHECK_THROWS_AS(parse_request_head("NOT A REQUEST LINE\r\nHost: x"), ParseError);
}

// --- Content-Length and keep-alive ------------------------------------------

TEST_CASE("content_length defaults to zero and parses numbers") {
    CHECK_EQ(parse_request_head("GET / HTTP/1.1").content_length(), std::size_t{0});
    CHECK_EQ(parse_request_head("POST / HTTP/1.1\r\nContent-Length: 42").content_length(),
             std::size_t{42});
}

TEST_CASE("content_length rejects invalid values") {
    CHECK_THROWS_AS(parse_request_head("POST / HTTP/1.1\r\nContent-Length: abc").content_length(),
                    ParseError);
    CHECK_THROWS_AS(parse_request_head("POST / HTTP/1.1\r\nContent-Length: -1").content_length(),
                    ParseError);
    CHECK_THROWS_AS(parse_request_head("POST / HTTP/1.1\r\nContent-Length: 12x").content_length(),
                    ParseError);
    CHECK_THROWS_AS(parse_request_head("POST / HTTP/1.1\r\nContent-Length:").content_length(),
                    ParseError);
    // Two different lengths are combined into "1, 2", which is invalid.
    CHECK_THROWS_AS(parse_request_head("POST / HTTP/1.1\r\nContent-Length: 1\r\n"
                                       "Content-Length: 2")
                        .content_length(),
                    ParseError);
}

TEST_CASE("keep_alive follows the HTTP version and Connection header") {
    CHECK(parse_request_head("GET / HTTP/1.1").keep_alive());
    CHECK_FALSE(parse_request_head("GET / HTTP/1.1\r\nConnection: close").keep_alive());
    CHECK_FALSE(parse_request_head("GET / HTTP/1.1\r\nConnection: Keep-Alive, CLOSE").keep_alive());
    CHECK_FALSE(parse_request_head("GET / HTTP/1.0").keep_alive());
    CHECK(parse_request_head("GET / HTTP/1.0\r\nConnection: keep-alive").keep_alive());
    CHECK(parse_request_head("GET / HTTP/1.0\r\nConnection: Upgrade, Keep-Alive").keep_alive());
}

// --- RequestReader (head + body over a socket) --------------------------------

TEST_CASE("RequestReader reads a request with a body") {
    SocketPair pair = make_socket_pair();
    pair.client.send_all("POST /echo HTTP/1.1\r\nContent-Length: 11\r\n\r\nhello world");

    http::RequestReader reader(pair.server);
    const auto request = reader.next();
    REQUIRE(request.has_value());
    CHECK(request->method == Method::Post);
    CHECK_EQ(request->path, "/echo");
    CHECK_EQ(request->body, "hello world");
    CHECK_FALSE(reader.has_buffered_data());
}

TEST_CASE("RequestReader keeps pipelined requests apart") {
    SocketPair pair = make_socket_pair();
    pair.client.send_all(
        "POST /a HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc"
        "\r\n" // stray CRLF between requests is allowed
        "GET /b HTTP/1.1\r\nHost: x\r\n\r\n");
    pair.client.close();

    http::RequestReader reader(pair.server);
    const auto first = reader.next();
    REQUIRE(first.has_value());
    CHECK_EQ(first->path, "/a");
    CHECK_EQ(first->body, "abc");
    CHECK(reader.has_buffered_data());

    const auto second = reader.next();
    REQUIRE(second.has_value());
    CHECK_EQ(second->path, "/b");
    CHECK_EQ(second->body, "");

    CHECK_EQ(reader.next(), std::nullopt); // clean end of stream
}

TEST_CASE("RequestReader reports truncated requests") {
    {
        SocketPair pair = make_socket_pair();
        pair.client.send_all("GET / HTTP/1.1\r\nHost: x\r\n");
        pair.client.close();
        http::RequestReader reader(pair.server);
        CHECK_THROWS_AS(reader.next(), ParseError);
    }
    {
        SocketPair pair = make_socket_pair();
        pair.client.send_all("POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nshort");
        pair.client.close();
        http::RequestReader reader(pair.server);
        CHECK_THROWS_AS(reader.next(), ParseError);
    }
}

TEST_CASE("RequestReader enforces head and body limits") {
    const http::RequestLimits limits{64, 8};
    {
        SocketPair pair = make_socket_pair();
        pair.client.send_all("GET / HTTP/1.1\r\nX-Long: " + std::string(100, 'a') + "\r\n\r\n");
        http::RequestReader reader(pair.server, limits);
        CHECK_THROWS_AS(reader.next(), http::RequestTooLarge);
    }
    {
        SocketPair pair = make_socket_pair();
        pair.client.send_all("POST / HTTP/1.1\r\nContent-Length: 9\r\n\r\n123456789");
        http::RequestReader reader(pair.server, limits);
        CHECK_THROWS_AS(reader.next(), http::RequestTooLarge);
    }
}

TEST_CASE("RequestReader rejects chunked transfer encoding") {
    SocketPair pair = make_socket_pair();
    pair.client.send_all("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
    http::RequestReader reader(pair.server);
    CHECK_THROWS_AS(reader.next(), ParseError);
}

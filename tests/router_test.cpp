#include <stdexcept>
#include <string>
#include <utility>

#include "http_request.hpp"
#include "http_response.hpp"
#include "router.hpp"
#include "status.hpp"
#include "test_framework.hpp"

using http::HttpRequest;
using http::HttpResponse;
using http::Method;
using http::Router;

namespace {

HttpRequest make_request(Method method, std::string path) {
    HttpRequest request;
    request.method = method;
    request.target = path;
    request.path = std::move(path);
    request.version = "HTTP/1.1";
    return request;
}

// A handler that answers with a fixed body, so tests can tell which route ran.
http::Handler reply(std::string body) {
    return [body = std::move(body)](const HttpRequest&) {
        return HttpResponse::text(http::status::kOk, body);
    };
}

// A handler that echoes the captured path parameters "a" and "b".
HttpResponse echo_params(const HttpRequest& request) {
    return HttpResponse::text(http::status::kOk, request.param("a").value_or("-") + "," +
                                                     request.param("b").value_or("-"));
}

} // namespace

// --- Exact routes ------------------------------------------------------------

TEST_CASE("Router dispatches exact routes by method and path") {
    Router router;
    router.get("/", reply("home")).get("/about", reply("about")).post("/about", reply("posted"));

    CHECK_EQ(router.route(make_request(Method::Get, "/")).body(), "home");
    CHECK_EQ(router.route(make_request(Method::Get, "/about")).body(), "about");
    CHECK_EQ(router.route(make_request(Method::Post, "/about")).body(), "posted");
    CHECK_EQ(router.size(), std::size_t{3});
}

TEST_CASE("Router matches paths exactly, including trailing slashes") {
    Router router;
    router.get("/about", reply("about"));

    CHECK_EQ(router.route(make_request(Method::Get, "/about/")).status(), http::status::kNotFound);
    CHECK_EQ(router.route(make_request(Method::Get, "/About")).status(), http::status::kNotFound);
    CHECK_EQ(router.route(make_request(Method::Get, "/about/x")).status(), http::status::kNotFound);
}

TEST_CASE("Router replaces a handler registered twice for the same route") {
    Router router;
    router.get("/x", reply("first")).get("/x", reply("second"));

    CHECK_EQ(router.route(make_request(Method::Get, "/x")).body(), "second");
    CHECK_EQ(router.size(), std::size_t{1});
}

// --- 404 and 405 -------------------------------------------------------------

TEST_CASE("Router answers 404 for unknown paths without a fallback") {
    Router router;
    router.get("/", reply("home"));

    const HttpResponse response = router.route(make_request(Method::Get, "/missing"));
    CHECK_EQ(response.status(), http::status::kNotFound);
    CHECK(response.is_generated_error());
    CHECK_EQ(response.error_detail(), "/missing");
}

TEST_CASE("Router answers 405 with a sorted Allow header for wrong methods") {
    Router router;
    router.post("/items", reply("create")).get("/items", reply("list")).del("/items", reply("x"));

    const HttpResponse response = router.route(make_request(Method::Put, "/items"));
    CHECK_EQ(response.status(), http::status::kMethodNotAllowed);
    CHECK_EQ(response.header("Allow"), "GET, HEAD, POST, DELETE");
}

TEST_CASE("Router does not advertise HEAD in Allow without a GET route") {
    Router router;
    router.post("/upload", reply("ok"));

    const HttpResponse response = router.route(make_request(Method::Get, "/upload"));
    CHECK_EQ(response.status(), http::status::kMethodNotAllowed);
    CHECK_EQ(response.header("Allow"), "POST");
}

TEST_CASE("Router serves HEAD from the GET handler unless HEAD is registered") {
    Router router;
    router.get("/a", reply("get-a")).get("/b", reply("get-b")).add(Method::Head, "/b", reply("head-b"));

    CHECK_EQ(router.route(make_request(Method::Head, "/a")).body(), "get-a");
    CHECK_EQ(router.route(make_request(Method::Head, "/b")).body(), "head-b");
}

// --- Fallback ----------------------------------------------------------------

TEST_CASE("Router sends unknown paths to the fallback handler") {
    Router router;
    router.get("/api", reply("api")).fallback([](const HttpRequest& request) {
        return HttpResponse::text(http::status::kOk, "fallback " + request.path);
    });

    CHECK_EQ(router.route(make_request(Method::Get, "/index.html")).body(), "fallback /index.html");
    CHECK_EQ(router.route(make_request(Method::Get, "/api")).body(), "api");
}

TEST_CASE("Router prefers 405 over the fallback for known paths") {
    Router router;
    router.get("/api", reply("api")).fallback(reply("fallback"));

    CHECK_EQ(router.route(make_request(Method::Post, "/api")).status(),
             http::status::kMethodNotAllowed);
}

// --- Path parameters ---------------------------------------------------------

TEST_CASE("Router captures path parameters") {
    Router router;
    router.get("/users/:a/posts/:b", echo_params);

    const HttpResponse response = router.route(make_request(Method::Get, "/users/42/posts/hello"));
    CHECK_EQ(response.status(), http::status::kOk);
    CHECK_EQ(response.body(), "42,hello");
}

TEST_CASE("Router parameters do not match empty or extra segments") {
    Router router;
    router.get("/users/:a", echo_params);

    CHECK_EQ(router.route(make_request(Method::Get, "/users/")).status(), http::status::kNotFound);
    CHECK_EQ(router.route(make_request(Method::Get, "/users")).status(), http::status::kNotFound);
    CHECK_EQ(router.route(make_request(Method::Get, "/users/1/2")).status(),
             http::status::kNotFound);
}

TEST_CASE("Router prefers exact routes over parameterised ones") {
    Router router;
    router.get("/users/:a", echo_params).get("/users/me", reply("me"));

    CHECK_EQ(router.route(make_request(Method::Get, "/users/me")).body(), "me");
    CHECK_EQ(router.route(make_request(Method::Get, "/users/7")).body(), "7,-");
}

TEST_CASE("Router tries parameterised routes in registration order") {
    Router router;
    router.get("/files/:a/raw", reply("raw")).get("/files/:a/:b", echo_params);

    CHECK_EQ(router.route(make_request(Method::Get, "/files/x/raw")).body(), "raw");
    CHECK_EQ(router.route(make_request(Method::Get, "/files/x/meta")).body(), "x,meta");
}

TEST_CASE("Router falls through to a later pattern when the method differs") {
    Router router;
    router.post("/items/:a", reply("post")).get("/items/:b", echo_params);

    CHECK_EQ(router.route(make_request(Method::Get, "/items/5")).body(), "-,5");
    CHECK_EQ(router.route(make_request(Method::Post, "/items/5")).body(), "post");
}

TEST_CASE("Router lists methods from matching patterns in the Allow header") {
    Router router;
    router.get("/items/:a", echo_params).put("/items/:a", reply("put"));

    const HttpResponse response = router.route(make_request(Method::Delete, "/items/1"));
    CHECK_EQ(response.status(), http::status::kMethodNotAllowed);
    CHECK_EQ(response.header("Allow"), "GET, HEAD, PUT");
}

TEST_CASE("Router does not leak parameters into the caller's request") {
    Router router;
    router.get("/users/:a", echo_params);

    const HttpRequest request = make_request(Method::Get, "/users/9");
    CHECK_EQ(router.route(request).body(), "9,-");
    CHECK(request.params.empty());
    CHECK_EQ(request.param("a"), std::nullopt);
}

// --- Registration errors -----------------------------------------------------

TEST_CASE("Router rejects invalid route registrations") {
    Router router;
    CHECK_THROWS_AS(router.get("", reply("x")), std::invalid_argument);
    CHECK_THROWS_AS(router.get("users", reply("x")), std::invalid_argument);
    CHECK_THROWS_AS(router.get("/users", http::Handler{}), std::invalid_argument);
    CHECK_THROWS_AS(router.get("/users/:", reply("x")), std::invalid_argument);
    CHECK_THROWS_AS(router.get("/a/:id/b/:id", reply("x")), std::invalid_argument);
    CHECK_EQ(router.size(), std::size_t{0});
}

#include <cstdint>
#include <string>
#include <utility>

#include "demo_api.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "json.hpp"
#include "router.hpp"
#include "status.hpp"
#include "test_framework.hpp"

using http::HttpRequest;
using http::HttpResponse;
using http::Json;
using http::Method;

namespace {

// A router with only the demo API on it, plus its store.
struct Api {
    http::TodoStore store;
    http::Router router;

    Api() { http::register_demo_api(router, store); }

    HttpResponse call(Method method, const std::string& target, std::string body = {},
                      std::string content_type = "application/json") {
        HttpRequest request = http::parse_request_line(std::string(http::to_string(method)) +
                                                       " " + target + " HTTP/1.1");
        if (!body.empty()) {
            request.headers["content-type"] = std::move(content_type);
            request.body = std::move(body);
        }
        return router.route(request);
    }
};

Json json_of(const HttpResponse& response) {
    return Json::parse(response.body());
}

std::string error_message(const HttpResponse& response) {
    return json_of(response).at("error").at("message").as_string();
}

} // namespace

TEST_CASE("TodoStore assigns increasing ids and supports CRUD") {
    http::TodoStore store;
    const auto first = store.create("one");
    const auto second = store.create("two", true);
    CHECK_EQ(first.id, std::int64_t{1});
    CHECK_EQ(second.id, std::int64_t{2});
    CHECK_EQ(store.size(), std::size_t{2});

    const auto updated = store.update(1, std::string("uno"), true);
    REQUIRE(updated.has_value());
    CHECK_EQ(updated->title, "uno");
    CHECK(updated->done);
    CHECK_FALSE(store.update(99, std::nullopt, true).has_value());

    CHECK(store.remove(1));
    CHECK_FALSE(store.remove(1));
    CHECK_FALSE(store.get(1).has_value());
    CHECK_EQ(store.create("three").id, std::int64_t{3}); // ids are never reused
}

TEST_CASE("API lists todos and filters by done") {
    Api api;
    api.store.create("a");
    api.store.create("b", true);

    const auto all = api.call(Method::Get, "/api/todos");
    CHECK_EQ(all.status(), http::status::kOk);
    CHECK_EQ(all.header("Content-Type"), "application/json");
    CHECK_EQ(all.body(),
             "{\"todos\":[{\"id\":1,\"title\":\"a\",\"done\":false},"
             "{\"id\":2,\"title\":\"b\",\"done\":true}],\"count\":2}\n");

    const auto done = json_of(api.call(Method::Get, "/api/todos?done=true"));
    CHECK_EQ(done.at("count").as_integer(), std::int64_t{1});
    CHECK_EQ(done.at("todos").as_array()[0].at("title").as_string(), "b");

    CHECK_EQ(api.call(Method::Get, "/api/todos?done=maybe").status(), http::status::kBadRequest);
}

TEST_CASE("API creates a todo with 201 and a Location header") {
    Api api;
    const auto response = api.call(Method::Post, "/api/todos", R"({"title": "  Buy milk "})");
    CHECK_EQ(response.status(), http::status::kCreated);
    CHECK_EQ(response.header("Location"), "/api/todos/1");
    CHECK_EQ(response.body(), "{\"id\":1,\"title\":\"Buy milk\",\"done\":false}\n");
    CHECK_EQ(api.store.size(), std::size_t{1});
}

TEST_CASE("API validates the request body") {
    Api api;
    CHECK_EQ(api.call(Method::Post, "/api/todos", "{\"title\": 1}").status(),
             http::status::kBadRequest);
    CHECK_EQ(error_message(api.call(Method::Post, "/api/todos", "{}")), "missing field 'title'");
    CHECK_EQ(error_message(api.call(Method::Post, "/api/todos", R"({"title": " "})")),
             "'title' must not be empty");
    CHECK_EQ(error_message(api.call(Method::Post, "/api/todos", R"({"title": "x", "dne": 1})")),
             "unknown field 'dne'");
    CHECK_EQ(error_message(api.call(Method::Post, "/api/todos", "[1]")),
             "request body must be a JSON object");

    const auto malformed = api.call(Method::Post, "/api/todos", "{\"title\": ");
    CHECK_EQ(malformed.status(), http::status::kBadRequest);
    CHECK(error_message(malformed).find("invalid JSON") == 0);

    const auto wrong_type = api.call(Method::Post, "/api/todos", "title=x", "text/plain");
    CHECK_EQ(wrong_type.status(), http::status::kUnsupportedMediaType);
    CHECK_EQ(api.call(Method::Post, "/api/todos", R"({"title":"x"})",
                      "Application/JSON; charset=utf-8")
                 .status(),
             http::status::kCreated);
    CHECK_EQ(api.store.size(), std::size_t{1});
}

TEST_CASE("API fetches, updates and deletes by id") {
    Api api;
    api.store.create("write tests");

    CHECK_EQ(json_of(api.call(Method::Get, "/api/todos/1")).at("title").as_string(),
             "write tests");

    const auto patched = api.call(Method::Patch, "/api/todos/1", R"({"done": true})");
    CHECK_EQ(patched.status(), http::status::kOk);
    CHECK_EQ(patched.body(), "{\"id\":1,\"title\":\"write tests\",\"done\":true}\n");
    CHECK_EQ(api.call(Method::Patch, "/api/todos/1", R"({"done": "yes"})").status(),
             http::status::kBadRequest);

    const auto deleted = api.call(Method::Delete, "/api/todos/1");
    CHECK_EQ(deleted.status(), http::status::kNoContent);
    CHECK(deleted.body().empty());
    CHECK_EQ(api.call(Method::Delete, "/api/todos/1").status(), http::status::kNotFound);
}

TEST_CASE("API answers unknown ids with a JSON 404") {
    Api api;
    for (const char* target : {"/api/todos/7", "/api/todos/abc", "/api/todos/0", "/api/todos/-1"}) {
        const auto response = api.call(Method::Get, target);
        CHECK_EQ(response.status(), http::status::kNotFound);
        CHECK_EQ(response.header("Content-Type"), "application/json");
        CHECK_FALSE(response.is_generated_error());
    }
    CHECK_EQ(error_message(api.call(Method::Get, "/api/todos/7")), "no todo with id 7");
}

TEST_CASE("API exposes status and an endpoint index") {
    Api api;
    const Json status = json_of(api.call(Method::Get, "/api/status"));
    CHECK_EQ(status.at("name").as_string(), "cpp-http-server");
    CHECK(status.at("uptime_seconds").is_number());
    CHECK_EQ(status.at("todos").as_integer(), std::int64_t{0});

    const Json index = json_of(api.call(Method::Get, "/api"));
    CHECK_EQ(index.at("endpoints").size(), std::size_t{6});

    CHECK_EQ(api.call(Method::Put, "/api/todos/1").status(), http::status::kMethodNotAllowed);
}

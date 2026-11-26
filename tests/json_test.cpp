#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "http_response.hpp"
#include "json.hpp"
#include "status.hpp"
#include "test_framework.hpp"

using http::Json;
using http::JsonError;

// --- Building and serializing ------------------------------------------------

TEST_CASE("Json serializes scalars") {
    CHECK_EQ(Json().dump(), "null");
    CHECK_EQ(Json(nullptr).dump(), "null");
    CHECK_EQ(Json(true).dump(), "true");
    CHECK_EQ(Json(false).dump(), "false");
    CHECK_EQ(Json(42).dump(), "42");
    CHECK_EQ(Json(-7L).dump(), "-7");
    CHECK_EQ(Json(std::size_t{3}).dump(), "3");
    CHECK_EQ(Json(1.5).dump(), "1.5");
    CHECK_EQ(Json(0.1).dump(), "0.1");
    CHECK_EQ(Json(3.0).dump(), "3.0");
    CHECK_EQ(Json("hi").dump(), "\"hi\"");
}

TEST_CASE("Json writes non-finite doubles as null") {
    CHECK_EQ(Json(std::numeric_limits<double>::infinity()).dump(), "null");
    CHECK_EQ(Json(std::nan("")).dump(), "null");
}

TEST_CASE("Json escapes strings") {
    CHECK_EQ(Json("say \"hi\"\\").dump(), R"("say \"hi\"\\")");
    CHECK_EQ(Json("a\nb\tc\r").dump(), R"("a\nb\tc\r")");
    CHECK_EQ(Json(std::string("\x01\x1f", 2)).dump(), R"("\u0001\u001f")");
    CHECK_EQ(Json("caf\xC3\xA9").dump(), "\"caf\xC3\xA9\"");
}

TEST_CASE("Json objects keep insertion order") {
    const Json value = Json::object({{"b", 1}, {"a", 2}, {"c", Json::array({1, "x", nullptr})}});
    CHECK_EQ(value.dump(), R"({"b":1,"a":2,"c":[1,"x",null]})");
    CHECK_EQ(Json::object().dump(), "{}");
    CHECK_EQ(Json::array().dump(), "[]");
}

TEST_CASE("Json operator[] and push_back build values in place") {
    Json value;
    value["name"] = "Ada";
    value["tags"].push_back("math");
    value["tags"].push_back("engines");
    value["name"] = "Ada Lovelace"; // replaces, keeps position
    CHECK_EQ(value.dump(), R"({"name":"Ada Lovelace","tags":["math","engines"]})");
    CHECK_EQ(value.size(), std::size_t{2});
    CHECK_THROWS_AS(Json(1).push_back(2), JsonError);
}

TEST_CASE("Json pretty-prints with an indent") {
    const Json value = Json::object({{"a", Json::array({1, 2})}, {"b", Json::object()}});
    CHECK_EQ(value.dump(2), "{\n  \"a\": [\n    1,\n    2\n  ],\n  \"b\": {}\n}");
}

// --- Parsing -----------------------------------------------------------------

TEST_CASE("Json::parse reads every value type") {
    const Json value = Json::parse(
        R"( {"n": null, "t": true, "f": false, "i": -12, "d": 2.5e2, "s": "x", "a": [1, [2]], "o": {}} )");
    REQUIRE(value.is_object());
    CHECK(value.at("n").is_null());
    CHECK_EQ(value.at("t").as_bool(), true);
    CHECK_EQ(value.at("f").as_bool(), false);
    CHECK(value.at("i").is_integer());
    CHECK_EQ(value.at("i").as_integer(), std::int64_t{-12});
    CHECK_EQ(value.at("d").as_double(), 250.0);
    CHECK_EQ(value.at("s").as_string(), "x");
    CHECK_EQ(value.at("a").size(), std::size_t{2});
    CHECK(value.at("o").is_object());
    CHECK(value.find("missing") == nullptr);
}

TEST_CASE("Json::parse decodes string escapes and surrogate pairs") {
    CHECK_EQ(Json::parse(R"("a\"b\\c\/d\n")").as_string(), "a\"b\\c/d\n");
    CHECK_EQ(Json::parse(R"("\u00e9")").as_string(), "\xC3\xA9");
    CHECK_EQ(Json::parse(R"("\u015Fehir")").as_string(), "\xC5\x9F" "ehir");
    CHECK_EQ(Json::parse(R"("\ud83d\ude00")").as_string(), "\xF0\x9F\x98\x80");
}

TEST_CASE("Json::parse round-trips its own output") {
    const std::string text = R"({"id":7,"title":"tab\tand \"quotes\"","ratio":0.25,"list":[true,null]})";
    CHECK_EQ(Json::parse(text).dump(), text);
}

TEST_CASE("Json::parse keeps big integers exact and falls back to double") {
    CHECK_EQ(Json::parse("9223372036854775807").as_integer(),
             std::numeric_limits<std::int64_t>::max());
    const Json big = Json::parse("92233720368547758080");
    CHECK_FALSE(big.is_integer());
    CHECK(big.is_number());
}

TEST_CASE("Json::parse rejects malformed documents") {
    const char* bad[] = {
        "",         "   ",       "{",          "[1,]",        "{\"a\" 1}",  "{\"a\":1,}",
        "{a:1}",    "'x'",       "tru",        "nul",         "01",         "1.",
        ".5",       "-",         "1e",         "\"abc",       "\"\\x\"",    "\"\\u12\"",
        "\"\\ud83d\"", "\"a\nb\"", "[1] [2]",  "1e999",
    };
    for (const char* text : bad) {
        bool threw = false;
        try {
            (void)Json::parse(text);
        } catch (const JsonError&) {
            threw = true;
        }
        if (!threw) {
            ::test::report_failure(__FILE__, __LINE__, std::string("accepted: ") + text);
        }
    }
}

TEST_CASE("Json::parse reports the offset of the problem") {
    try {
        (void)Json::parse("{\"a\": 1 \"b\"}");
        CHECK(false);
    } catch (const JsonError& error) {
        CHECK_EQ(std::string(error.what()), "invalid JSON at offset 8: expected ',' or '}'");
    }
}

TEST_CASE("Json::parse limits nesting depth") {
    CHECK_NOTHROW(Json::parse(std::string(60, '[') + std::string(60, ']')));
    CHECK_THROWS_AS(Json::parse(std::string(1000, '[') + std::string(1000, ']')), JsonError);
}

TEST_CASE("Json::parse lets the last duplicate key win") {
    const Json value = Json::parse(R"({"a": 1, "a": 2})");
    CHECK_EQ(value.size(), std::size_t{1});
    CHECK_EQ(value.at("a").as_integer(), std::int64_t{2});
}

// --- Accessors and comparison ------------------------------------------------

TEST_CASE("Json accessors throw on type mismatch") {
    CHECK_THROWS_AS(Json("x").as_integer(), JsonError);
    CHECK_THROWS_AS(Json(1).as_string(), JsonError);
    CHECK_THROWS_AS(Json(1.5).as_integer(), JsonError);
    CHECK_THROWS_AS(Json::object().at("missing"), JsonError);
    CHECK_EQ(Json(4.0).as_integer(), std::int64_t{4});
    CHECK_EQ(Json(4).as_double(), 4.0);
}

TEST_CASE("Json compares by value") {
    CHECK(Json::parse(R"({"a":[1,2]})") == Json::object({{"a", Json::array({1, 2})}}));
    CHECK(Json(1) == Json(1.0));
    CHECK(Json("1") != Json(1));
    CHECK(Json() == Json(nullptr));
}

// --- HttpResponse helpers ----------------------------------------------------

TEST_CASE("HttpResponse::json sets the body and content type") {
    const auto response =
        http::HttpResponse::json(http::status::kCreated, Json::object({{"id", 1}}));
    CHECK_EQ(response.status(), http::status::kCreated);
    CHECK_EQ(response.body(), "{\"id\":1}\n");
    CHECK_EQ(response.header("Content-Type"), "application/json");
    CHECK_FALSE(response.is_generated_error());
}

TEST_CASE("HttpResponse::json_error wraps status and message") {
    CHECK_EQ(http::HttpResponse::json_error(404).body(),
             "{\"error\":{\"status\":404,\"message\":\"Not Found\"}}\n");
    CHECK_EQ(http::HttpResponse::json_error(400, "bad \"title\"").body(),
             "{\"error\":{\"status\":400,\"message\":\"bad \\\"title\\\"\"}}\n");
}

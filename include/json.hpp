#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace http {

// Thrown by Json::parse() for malformed input and by the accessors when a
// value has a different type than requested.
class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A JSON value: null, boolean, number, string, array or object.
//
//   Json user = Json::object({{"id", 42}, {"name", "Ada"}, {"admin", true}});
//   user["tags"] = Json::array({"math", "engines"});
//   user.dump();   // {"id":42,"name":"Ada","admin":true,"tags":["math","engines"]}
//   user.dump(2);  // the same, pretty-printed with two-space indentation
//
//   Json parsed = Json::parse(R"({"title": "Buy milk", "done": false})");
//   parsed.at("title").as_string();  // "Buy milk"
//
// Objects keep their keys in insertion order, so responses come out in the
// order they were built. Integers are stored exactly (int64) and printed
// without a decimal point; other numbers are doubles.
class Json {
public:
    enum class Type { Null, Bool, Integer, Double, String, Array, Object };

    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;

    Json() noexcept = default;
    Json(std::nullptr_t) noexcept {}
    Json(bool value) noexcept : value_(value) {}
    Json(double value) noexcept : value_(value) {}
    Json(float value) noexcept : value_(static_cast<double>(value)) {}
    Json(const char* value) : value_(std::string(value)) {}
    Json(std::string value) : value_(std::move(value)) {}
    Json(std::string_view value) : value_(std::string(value)) {}
    Json(Array value) : value_(std::move(value)) {}
    Json(Object value) : value_(std::move(value)) {}

    // Any integer type except bool (which has its own constructor).
    template <typename T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Json(T value) noexcept : value_(static_cast<std::int64_t>(value)) {}

    static Json array(std::initializer_list<Json> items = {}) { return Json(Array(items)); }
    static Json object(std::initializer_list<Member> members = {}) {
        return Json(Object(members));
    }

    // Parses a complete JSON document (RFC 8259). Throws JsonError with the
    // byte offset of the problem, e.g. "invalid JSON at offset 12: expected ':'".
    static Json parse(std::string_view text);

    Type type() const noexcept { return static_cast<Type>(value_.index()); }
    bool is_null() const noexcept { return type() == Type::Null; }
    bool is_bool() const noexcept { return type() == Type::Bool; }
    bool is_integer() const noexcept { return type() == Type::Integer; }
    bool is_number() const noexcept { return is_integer() || type() == Type::Double; }
    bool is_string() const noexcept { return type() == Type::String; }
    bool is_array() const noexcept { return type() == Type::Array; }
    bool is_object() const noexcept { return type() == Type::Object; }

    // Typed accessors; throw JsonError on a type mismatch.
    bool as_bool() const;
    std::int64_t as_integer() const; // also accepts doubles with no fraction
    double as_double() const;        // any number
    const std::string& as_string() const;
    const Array& as_array() const;
    Array& as_array();
    const Object& as_object() const;
    Object& as_object();

    // Object member lookup: nullptr if absent (or if this is not an object).
    const Json* find(std::string_view key) const noexcept;
    bool contains(std::string_view key) const noexcept { return find(key) != nullptr; }
    // Like find(), but throws JsonError if the member is missing.
    const Json& at(std::string_view key) const;
    // Inserts a null member if missing; a null value turns into an object.
    Json& operator[](std::string_view key);

    // Appends to an array; a null value turns into an array.
    Json& push_back(Json value);

    // Number of array elements or object members, 0 for everything else.
    std::size_t size() const noexcept;

    // Serializes the value. indent < 0 gives compact output, otherwise
    // nested values are put on their own lines, indented by `indent` spaces.
    // Non-finite doubles (NaN, infinity) are written as null.
    std::string dump(int indent = -1) const;

    friend bool operator==(const Json& a, const Json& b);
    friend bool operator!=(const Json& a, const Json& b) { return !(a == b); }

private:
    void dump_to(std::string& out, int indent, int depth) const;

    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> value_;
};

// Name of a type for error messages: "null", "boolean", "number", ...
std::string_view to_string(Json::Type type) noexcept;

// Writes `text` as a quoted JSON string, escaping quotes, backslashes and
// control characters. UTF-8 is passed through unchanged.
void append_json_string(std::string& out, std::string_view text);

} // namespace http

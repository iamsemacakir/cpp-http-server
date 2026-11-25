#pragma once

// A deliberately small unit test framework: self-registering test cases,
// CHECK/REQUIRE assertions with readable failure messages, and a runner
// that can filter tests by name. No external dependencies.

#include <exception>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace test {

using TestFunction = void (*)();

struct TestCase {
    std::string name;
    TestFunction function;
    const char* file;
    int line;
};

// All registered test cases, in registration order.
std::vector<TestCase>& registry();

// Registers a test case at static-initialisation time (used by TEST_CASE).
struct Registrar {
    Registrar(const char* name, TestFunction function, const char* file, int line);
};

// Thrown by REQUIRE-style assertions to abort the current test case.
class AssertionAbort : public std::exception {
public:
    const char* what() const noexcept override { return "assertion failed"; }
};

// Records a failed assertion for the test case that is currently running.
void report_failure(const char* file, int line, std::string_view message);

// Runs the registered tests whose name contains `filter` (all if empty).
// Returns the process exit code: 0 if every test passed, 1 otherwise.
int run_all(std::string_view filter = {});

// Command-line entry point: `[--list] [filter]`.
int run_main(int argc, char** argv);

namespace detail {

template <typename T, typename = void>
struct is_printable : std::false_type {};

template <typename T>
struct is_printable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <typename T>
std::string describe(const T& value);

template <typename T>
std::string describe_optional(const std::optional<T>& value) {
    return value ? describe(*value) : std::string("nullopt");
}

template <typename T>
std::string describe(const T& value) {
    if constexpr (std::is_same_v<T, std::nullopt_t>) {
        return "nullopt";
    } else if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        std::ostringstream out;
        out << '"';
        for (const char c : std::string_view(value)) {
            switch (c) {
            case '\r': out << "\\r"; break;
            case '\n': out << "\\n"; break;
            case '\t': out << "\\t"; break;
            case '"': out << "\\\""; break;
            default: out << c; break;
            }
        }
        out << '"';
        return out.str();
    } else if constexpr (std::is_enum_v<T>) {
        return "enum(" + std::to_string(static_cast<std::underlying_type_t<T>>(value)) + ")";
    } else if constexpr (is_printable<T>::value) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "<unprintable>";
    }
}

template <typename T>
std::string describe(const std::optional<T>& value) {
    return describe_optional(value);
}

template <typename A, typename B>
bool check_equal(const A& actual, const B& expected, const char* actual_text,
                 const char* expected_text, const char* file, int line) {
    if (actual == expected) {
        return true;
    }
    std::ostringstream message;
    message << "CHECK_EQ(" << actual_text << ", " << expected_text << ")\n"
            << "      actual:   " << describe(actual) << "\n"
            << "      expected: " << describe(expected);
    report_failure(file, line, message.str());
    return false;
}

} // namespace detail

} // namespace test

#define TEST_DETAIL_CONCAT_INNER(a, b) a##b
#define TEST_DETAIL_CONCAT(a, b) TEST_DETAIL_CONCAT_INNER(a, b)
#define TEST_DETAIL_CASE(function, name)                                                        \
    static void function();                                                                     \
    static const ::test::Registrar TEST_DETAIL_CONCAT(function, _registrar){name, &function,   \
                                                                            __FILE__, __LINE__}; \
    static void function()

// Defines a test case: TEST_CASE("parses the request line") { ... }
#define TEST_CASE(name) TEST_DETAIL_CASE(TEST_DETAIL_CONCAT(test_case_, __LINE__), name)

// Non-fatal assertions: record the failure and keep running the test.
#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) {                                                  \
            ::test::report_failure(__FILE__, __LINE__, "CHECK(" #expr ")"); \
        }                                                               \
    } while (false)

#define CHECK_FALSE(expr)                                                     \
    do {                                                                      \
        if (expr) {                                                           \
            ::test::report_failure(__FILE__, __LINE__, "CHECK_FALSE(" #expr ")"); \
        }                                                                     \
    } while (false)

#define CHECK_EQ(actual, expected) \
    ((void)::test::detail::check_equal((actual), (expected), #actual, #expected, __FILE__, __LINE__))

#define CHECK_THROWS_AS(expr, exception_type)                                                  \
    do {                                                                                       \
        bool test_detail_caught = false;                                                       \
        try {                                                                                  \
            (void)(expr);                                                                      \
        } catch (const exception_type&) {                                                      \
            test_detail_caught = true;                                                         \
        } catch (...) {                                                                        \
            ::test::report_failure(__FILE__, __LINE__,                                         \
                                   "CHECK_THROWS_AS(" #expr ", " #exception_type               \
                                   "): threw a different exception");                          \
            test_detail_caught = true;                                                         \
        }                                                                                      \
        if (!test_detail_caught) {                                                             \
            ::test::report_failure(__FILE__, __LINE__,                                         \
                                   "CHECK_THROWS_AS(" #expr ", " #exception_type "): no throw"); \
        }                                                                                      \
    } while (false)

#define CHECK_NOTHROW(expr)                                                                  \
    do {                                                                                     \
        try {                                                                                \
            (void)(expr);                                                                    \
        } catch (const std::exception& test_detail_error) {                                  \
            ::test::report_failure(__FILE__, __LINE__,                                       \
                                   std::string("CHECK_NOTHROW(" #expr "): threw: ") +        \
                                       test_detail_error.what());                            \
        } catch (...) {                                                                      \
            ::test::report_failure(__FILE__, __LINE__, "CHECK_NOTHROW(" #expr "): threw"); \
        }                                                                                    \
    } while (false)

// Fatal assertions: record the failure and stop the current test case.
#define REQUIRE(expr)                                                     \
    do {                                                                  \
        if (!(expr)) {                                                    \
            ::test::report_failure(__FILE__, __LINE__, "REQUIRE(" #expr ")"); \
            throw ::test::AssertionAbort{};                               \
        }                                                                 \
    } while (false)

#define REQUIRE_EQ(actual, expected)                                                             \
    do {                                                                                         \
        if (!::test::detail::check_equal((actual), (expected), #actual, #expected, __FILE__,     \
                                         __LINE__)) {                                            \
            throw ::test::AssertionAbort{};                                                      \
        }                                                                                        \
    } while (false)

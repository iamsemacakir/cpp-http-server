#include "test_framework.hpp"

#include <chrono>
#include <iostream>

namespace test {

namespace {

struct RunState {
    const TestCase* current = nullptr;
    int failures_in_current = 0;
};

RunState& state() {
    static RunState run_state;
    return run_state;
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [--list] [filter]\n"
              << "  filter   run only tests whose name contains this text\n"
              << "  --list   print the test names and exit\n";
}

} // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

Registrar::Registrar(const char* name, TestFunction function, const char* file, int line) {
    registry().push_back(TestCase{name, function, file, line});
}

void report_failure(const char* file, int line, std::string_view message) {
    RunState& run = state();
    ++run.failures_in_current;
    std::cout << "  " << file << ':' << line << ": " << message << '\n';
}

int run_all(std::string_view filter) {
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();

    int passed = 0;
    int failed = 0;
    for (const TestCase& test_case : registry()) {
        if (!filter.empty() && test_case.name.find(filter) == std::string::npos) {
            continue;
        }

        RunState& run = state();
        run.current = &test_case;
        run.failures_in_current = 0;

        try {
            test_case.function();
        } catch (const AssertionAbort&) {
            // Already reported; the test stops at the failed REQUIRE.
        } catch (const std::exception& error) {
            report_failure(test_case.file, test_case.line,
                           std::string("unexpected exception: ") + error.what());
        } catch (...) {
            report_failure(test_case.file, test_case.line, "unexpected non-standard exception");
        }

        if (run.failures_in_current == 0) {
            ++passed;
            std::cout << "[  OK  ] " << test_case.name << '\n';
        } else {
            ++failed;
            std::cout << "[ FAIL ] " << test_case.name << '\n';
        }
        run.current = nullptr;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    std::cout << '\n'
              << passed + failed << " test(s), " << passed << " passed, " << failed
              << " failed (" << elapsed << " ms)\n";

    if (passed + failed == 0) {
        std::cout << "no tests matched '" << filter << "'\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}

int run_main(int argc, char** argv) {
    std::string_view filter;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--list") {
            for (const TestCase& test_case : registry()) {
                std::cout << test_case.name << '\n';
            }
            return 0;
        }
        if (!filter.empty() || (!arg.empty() && arg.front() == '-')) {
            print_usage(argv[0]);
            return 2;
        }
        filter = arg;
    }
    return run_all(filter);
}

} // namespace test

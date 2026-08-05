#pragma once

// A minimal test harness.
//
// Deliberately dependency-free: Calcium's promise is portability across five
// platforms, and a test framework that must itself be ported on day one is a
// liability. This is ~100 lines and does what M0 needs. If the suite outgrows it,
// swapping in Catch2 or doctest is a contained change.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace ca::test {

struct TestCase {
    std::string_view name;
    void (*function)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failure_count() {
    static int count = 0;
    return count;
}

inline std::string_view& current_test_name() {
    static std::string_view name;
    return name;
}

inline void report_failure(const char* file, int line, const std::string& message) {
    ++failure_count();
    std::printf("  FAIL  %s\n        at %s:%d\n", message.c_str(), file, line);
}

struct Registrar {
    Registrar(std::string_view name, void (*function)()) {
        registry().push_back(TestCase{name, function});
    }
};

[[nodiscard]] inline bool nearly_equal(double a, double b, double tolerance) {
    if (std::isnan(a) || std::isnan(b)) {
        return false;
    }
    const double difference = std::abs(a - b);
    if (difference <= tolerance) {
        return true;
    }
    // Relative comparison for large magnitudes.
    const double scale = std::max(std::abs(a), std::abs(b));
    return difference <= tolerance * scale;
}

// Multi-argument assertions forward to these functions rather than expanding
// inline. The preprocessor splits macro arguments on every top-level comma,
// including commas inside braces, so a fixed-arity macro cannot accept
// CA_CHECK_NEAR(Point{3.0f, 4.0f}.magnitude(), 5.0, 1e-6). Passing the whole
// argument list through as __VA_ARGS__ into a call expression hands the parsing
// to the compiler, which knows braces group.

template <typename Actual, typename Expected>
void check_equal_impl(const char* file, int line, const char* expression,
                      const Actual& actual, const Expected& expected) {
    if (!(actual == expected)) {
        report_failure(file, line, std::string("expected ") + expression);
    }
}

inline void check_near_impl(const char* file, int line, const char* expression,
                            double actual, double expected, double tolerance) {
    if (!nearly_equal(actual, expected, tolerance)) {
        char buffer[320];
        std::snprintf(buffer, sizeof(buffer),
                      "%s\n        expected %.12g, got %.12g (tolerance %.3g)",
                      expression, expected, actual, tolerance);
        report_failure(file, line, buffer);
    }
}

inline int run_all() {
    std::printf("Running %zu test case(s)\n\n", registry().size());
    int failed_cases = 0;

    for (const TestCase& test_case : registry()) {
        current_test_name() = test_case.name;
        const int failures_before = failure_count();
        std::printf("[ RUN  ] %.*s\n",
                    static_cast<int>(test_case.name.size()), test_case.name.data());
        test_case.function();
        if (failure_count() == failures_before) {
            std::printf("[  OK  ]\n");
        } else {
            ++failed_cases;
            std::printf("[ FAIL ]\n");
        }
    }

    std::printf("\n%s: %d case(s) failed, %d assertion failure(s)\n",
                failed_cases == 0 ? "PASS" : "FAIL",
                failed_cases, failure_count());
    return failed_cases == 0 ? 0 : 1;
}

} // namespace ca::test

#define CA_TEST(name)                                                          \
    static void name();                                                        \
    static const ::ca::test::Registrar ca_registrar_##name{#name, &name};      \
    static void name()

// These macros are variadic so brace-initializers containing commas work
// unparenthesized: CA_CHECK(p == Point{1.0f, 2.0f}) would otherwise be parsed as
// two macro arguments. __VA_ARGS__ reassembles the expression, and /Zc:preprocessor
// makes MSVC's expansion conformant.

#define CA_CHECK(...)                                                          \
    do {                                                                       \
        if (!(__VA_ARGS__)) {                                                  \
            ::ca::test::report_failure(__FILE__, __LINE__,                     \
                                       "expected: " #__VA_ARGS__);             \
        }                                                                      \
    } while (false)

#define CA_CHECK_FALSE(...)                                                    \
    do {                                                                       \
        if ((__VA_ARGS__)) {                                                   \
            ::ca::test::report_failure(__FILE__, __LINE__,                     \
                                       "expected NOT: " #__VA_ARGS__);         \
        }                                                                      \
    } while (false)

#define CA_CHECK_EQUAL(...)                                                    \
    ::ca::test::check_equal_impl(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)

#define CA_CHECK_NEAR(...)                                                     \
    ::ca::test::check_near_impl(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)

#define CA_TEST_MAIN()                                                         \
    int main() { return ::ca::test::run_all(); }

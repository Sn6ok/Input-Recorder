#pragma once

// Minimal, dependency-free unit-test framework.
//
// Rationale (spec §478, §24, §358): the project must not pull in third-party
// dependencies without real need, and the build must work offline. A tiny
// in-repo harness gives us TEST_CASE registration + CHECK/REQUIRE assertions
// with readable diagnostics, and integrates with CTest via a single runner.
//
// Usage:
//     #include "framework/test_framework.hpp"
//     TEST_CASE("suite", "does a thing") {
//         CHECK(1 + 1 == 2);
//         CHECK_EQ(add(2, 3), 5);
//         REQUIRE(ptr != nullptr);   // aborts this test on failure
//     }
//
// A single translation unit must define the runner by including
// "framework/test_main.cpp" (done by the tests target).

#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace irtest {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

// Global registry of all test cases (populated at static-init time).
std::vector<TestCase>& registry();

// Registers a test case at static-initialization time.
struct Registrar {
    Registrar(std::string suite, std::string name, std::function<void()> fn) {
        registry().push_back({std::move(suite), std::move(name), std::move(fn)});
    }
};

// Thrown by REQUIRE to abort the current test immediately.
struct AbortTest {};

// Records a failure for the currently running test.
void record_failure(std::string message);

// Runs every registered test (optionally filtered by suite name).
// Returns the process exit code: 0 on success, 1 if any test failed.
int run_all(std::string_view suite_filter = {});

// ---- value stringification for diagnostics -------------------------------

template <typename T>
std::string to_debug_string(const T& value) {
    if constexpr (std::is_convertible_v<T, std::string_view>) {
        return std::string(std::string_view(value));
    } else if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(value);
    } else {
        std::ostringstream os;
        if constexpr (requires(std::ostringstream& s, const T& v) { s << v; }) {
            os << value;
        } else {
            os << "<value>";
        }
        return os.str();
    }
}

// ---- assertion helpers ---------------------------------------------------

inline void check_true(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::ostringstream os;
        os << file << ':' << line << ": CHECK(" << expr << ") failed";
        record_failure(os.str());
    }
}

inline void require_true(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::ostringstream os;
        os << file << ':' << line << ": REQUIRE(" << expr << ") failed";
        record_failure(os.str());
        throw AbortTest{};
    }
}

template <typename A, typename B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb,
              const char* file, int line) {
    if (!(a == b)) {
        std::ostringstream os;
        os << file << ':' << line << ": CHECK_EQ(" << ea << ", " << eb
           << ") failed [" << to_debug_string(a) << " != " << to_debug_string(b) << ']';
        record_failure(os.str());
    }
}

template <typename A, typename B>
void check_ne(const A& a, const B& b, const char* ea, const char* eb,
              const char* file, int line) {
    if (!(a != b)) {
        std::ostringstream os;
        os << file << ':' << line << ": CHECK_NE(" << ea << ", " << eb
           << ") failed [both == " << to_debug_string(a) << ']';
        record_failure(os.str());
    }
}

// Reports an unconditional failure with a caller-supplied message.
inline void fail(const std::string& msg, const char* file, int line) {
    std::ostringstream os;
    os << file << ':' << line << ": FAIL: " << msg;
    record_failure(os.str());
}

}  // namespace irtest

// ---- macros --------------------------------------------------------------

#define IR_CONCAT_INNER(a, b) a##b
#define IR_CONCAT(a, b) IR_CONCAT_INNER(a, b)

#define TEST_CASE(suite, name)                                                 \
    static void IR_CONCAT(ir_test_fn_, __LINE__)();                            \
    static ::irtest::Registrar IR_CONCAT(ir_test_reg_, __LINE__)(             \
        suite, name, &IR_CONCAT(ir_test_fn_, __LINE__));                       \
    static void IR_CONCAT(ir_test_fn_, __LINE__)()

#define CHECK(cond) ::irtest::check_true((cond), #cond, __FILE__, __LINE__)
#define REQUIRE(cond) ::irtest::require_true((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::irtest::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK_NE(a, b) ::irtest::check_ne((a), (b), #a, #b, __FILE__, __LINE__)
#define FAIL(msg) ::irtest::fail((msg), __FILE__, __LINE__)

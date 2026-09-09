// Runner implementation for the in-repo test framework. Linked once into the
// test executable; provides the registry, failure recording and main().

#include "framework/test_framework.hpp"

#include <cstring>
#include <iostream>

namespace irtest {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

namespace {
int g_current_failures = 0;
}

void record_failure(std::string message) {
    ++g_current_failures;
    std::cerr << "    " << message << '\n';
}

int run_all(std::string_view suite_filter) {
    int total = 0;
    int passed = 0;
    int failed = 0;

    for (const auto& tc : registry()) {
        if (!suite_filter.empty() && tc.suite != suite_filter) {
            continue;
        }
        ++total;
        g_current_failures = 0;
        std::cout << "[ RUN      ] " << tc.suite << " :: " << tc.name << '\n';
        try {
            tc.fn();
        } catch (const AbortTest&) {
            // Failure already recorded; test aborted early.
        } catch (const std::exception& ex) {
            record_failure(std::string("unexpected exception: ") + ex.what());
        } catch (...) {
            record_failure("unexpected non-standard exception");
        }
        if (g_current_failures == 0) {
            ++passed;
            std::cout << "[     PASS ] " << tc.suite << " :: " << tc.name << '\n';
        } else {
            ++failed;
            std::cout << "[     FAIL ] " << tc.suite << " :: " << tc.name << " ("
                      << g_current_failures << " check(s) failed)\n";
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "Total: " << total << "  Passed: " << passed
              << "  Failed: " << failed << '\n';
    std::cout << "========================================\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace irtest

int main(int argc, char** argv) {
    std::string_view filter;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--suite=", 8) == 0) {
            filter = argv[i] + 8;
        }
    }
    return irtest::run_all(filter);
}

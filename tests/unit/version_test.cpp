#include "framework/test_framework.hpp"

#include "core/version.hpp"

TEST_CASE("core", "version string is MAJOR.MINOR.PATCH") {
    CHECK_EQ(ir::version_string(), std::string("0.1.0"));
}

TEST_CASE("core", "app name is set") {
    CHECK(!ir::kAppName.empty());
    CHECK_EQ(std::string(ir::kAppName), std::string("Input Recorder"));
}

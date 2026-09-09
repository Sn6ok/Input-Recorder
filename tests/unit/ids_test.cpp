#include "framework/test_framework.hpp"

#include <unordered_map>

#include "core/ids.hpp"

TEST_CASE("core.ids", "default id is invalid, non-zero is valid") {
    ir::EventId a;
    CHECK(!a.valid());
    CHECK_EQ(a.value, 0u);

    ir::EventId b{7};
    CHECK(b.valid());
    CHECK_EQ(b.value, 7u);
}

TEST_CASE("core.ids", "comparison and ordering") {
    ir::SessionId a{1};
    ir::SessionId b{2};
    CHECK(a < b);
    CHECK(a != b);
    CHECK(ir::SessionId{2} == b);
    CHECK(b > a);
}

TEST_CASE("core.ids", "usable as unordered_map key") {
    std::unordered_map<ir::EventId, int> m;
    m[ir::EventId{42}] = 99;
    CHECK_EQ(m.at(ir::EventId{42}), 99);
}

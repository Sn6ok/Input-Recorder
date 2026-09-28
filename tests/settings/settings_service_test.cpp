#include "framework/test_framework.hpp"

#include "settings/settings_service.hpp"
#include "storage/event_store.hpp"

TEST_CASE("settings.service", "empty store yields validated defaults") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::SettingsService svc(store);
    REQUIRE(svc.load());
    CHECK(svc.config() == ir::Configuration{});
}

TEST_CASE("settings.service", "update persists and reloads") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    {
        ir::SettingsService svc(store);
        REQUIRE(svc.load());
        ir::Configuration c = svc.config();
        c.theme = ir::Theme::Dark;
        c.record_mouse_movement = true;
        c.retention_days = 30;
        CHECK(svc.update(c));
    }
    {
        ir::SettingsService svc2(store);
        REQUIRE(svc2.load());
        CHECK(svc2.config().theme == ir::Theme::Dark);
        CHECK(svc2.config().record_mouse_movement);
        CHECK_EQ(svc2.config().retention_days, 30);
    }
}

TEST_CASE("settings.service", "update validates out-of-range values") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::SettingsService svc(store);
    REQUIRE(svc.load());
    ir::Configuration c = svc.config();
    c.retention_days = 99999;             // clamps to 3650
    c.mouse_movement_sampling_ms = 1;     // clamps to 10
    svc.update(c);
    CHECK_EQ(svc.config().retention_days, 3650);
    CHECK_EQ(svc.config().mouse_movement_sampling_ms, 10);
}

TEST_CASE("settings.service", "edit mutates a single field") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::SettingsService svc(store);
    REQUIRE(svc.load());
    CHECK(svc.config().record_keyboard);
    svc.edit([](ir::Configuration& c) { c.record_keyboard = false; });
    CHECK(!svc.config().record_keyboard);
}

TEST_CASE("settings.service", "listeners are notified on update and can be removed") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::SettingsService svc(store);
    REQUIRE(svc.load());

    int calls = 0;
    ir::Theme seen = ir::Theme::System;
    const int id = svc.add_listener([&](const ir::Configuration& c) {
        ++calls;
        seen = c.theme;
    });

    svc.edit([](ir::Configuration& c) { c.theme = ir::Theme::Light; });
    CHECK_EQ(calls, 1);
    CHECK(seen == ir::Theme::Light);

    svc.remove_listener(id);
    svc.edit([](ir::Configuration& c) { c.theme = ir::Theme::Dark; });
    CHECK_EQ(calls, 1);  // no further notifications
}

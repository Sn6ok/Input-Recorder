#include "framework/test_framework.hpp"

#include <string>

#include "history/history_service.hpp"
#include "storage/event_store.hpp"

namespace {

ir::Event diagnostic(std::string message) {
    ir::Event e;
    e.id = ir::EventId{1};
    e.session = ir::SessionId{1};
    e.type = ir::EventType::Diagnostic;
    e.category = ir::EventCategory::SystemEvent;
    e.payload = ir::DiagnosticData{std::move(message)};
    return e;
}

}  // namespace

TEST_CASE("security", "diagnostic content is never exposed to search (spec 322)") {
    // Diagnostics must not carry or index user/secret text.
    CHECK(!ir::searchable_text(diagnostic("storage failure near secret-token"))
               .has_value());

    ir::EventStore store;
    REQUIRE(store.open_memory());
    store.insert_event(diagnostic("storage failure near secret-token"));
    ir::HistoryService h(store);
    // The diagnostic message is not searchable full-text.
    CHECK_EQ(h.count_search_events("secret", std::nullopt), 0);
    CHECK_EQ(h.count_search_events("token", std::nullopt), 0);
}

TEST_CASE("security", "FTS search input cannot inject query syntax") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::HistoryService h(store);

    // Classic injection-style strings must be treated as literal search text,
    // never as FTS/SQL operators, and must not error.
    const char* attacks[] = {
        "\" OR \"\"=\"", "*; DROP TABLE events;--", "()))(((", "NEAR/0", "^$."};
    for (const char* a : attacks) {
        // No throw, no crash; empty DB yields no hits.
        CHECK_EQ(h.search_events(a, std::nullopt, 10, 0).size(), 0u);
        CHECK_EQ(h.count_search_events(a, std::nullopt), 0);
    }

    // Every produced token is wrapped in quotes (so operators are inert).
    const std::string m = ir::make_fts_match("\" OR 1=1 --");
    CHECK(!m.empty());
    CHECK(m.front() == '"');
}

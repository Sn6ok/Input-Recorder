#include "framework/test_framework.hpp"

#include <string>

#include "storage/sqlite_database.hpp"
#include "storage/temp_db.hpp"

namespace {

void make_kv_table(ir::SqliteDatabase& db) {
    CHECK(db.exec("CREATE TABLE kv(k INTEGER PRIMARY KEY, v TEXT, b BLOB);"));
}

}  // namespace

TEST_CASE("storage.sqlite", "open memory, DDL, prepared insert and select") {
    ir::SqliteDatabase db;
    REQUIRE(db.open_memory());
    make_kv_table(db);

    ir::SqliteStatement ins = db.prepare("INSERT INTO kv(k,v) VALUES(?1,?2);");
    REQUIRE(ins.valid());
    ins.bind_int64(1, 42);
    ins.bind_text(2, "hello");
    CHECK(ins.step() == ir::SqliteStatement::Step::Done);
    CHECK_EQ(db.last_insert_rowid(), 42);

    ir::SqliteStatement sel = db.prepare("SELECT v FROM kv WHERE k=?1;");
    REQUIRE(sel.valid());
    sel.bind_int64(1, 42);
    REQUIRE(sel.step() == ir::SqliteStatement::Step::Row);
    CHECK_EQ(sel.column_text(0), std::string("hello"));
    CHECK(sel.step() == ir::SqliteStatement::Step::Done);
}

TEST_CASE("storage.sqlite", "blob round-trips including empty blobs") {
    ir::SqliteDatabase db;
    REQUIRE(db.open_memory());
    make_kv_table(db);

    std::vector<std::byte> blob;
    for (int i = 0; i < 256; ++i) blob.push_back(static_cast<std::byte>(i));

    ir::SqliteStatement ins = db.prepare("INSERT INTO kv(k,b) VALUES(?1,?2);");
    ins.bind_int64(1, 1);
    ins.bind_blob(2, blob);
    CHECK(ins.step() == ir::SqliteStatement::Step::Done);

    ins.reset();
    ins.bind_int64(1, 2);
    ins.bind_blob(2, {});  // empty blob
    CHECK(ins.step() == ir::SqliteStatement::Step::Done);

    ir::SqliteStatement sel = db.prepare("SELECT b FROM kv WHERE k=?1;");
    sel.bind_int64(1, 1);
    REQUIRE(sel.step() == ir::SqliteStatement::Step::Row);
    CHECK(sel.column_blob(0) == blob);

    sel.reset();
    sel.bind_int64(1, 2);
    REQUIRE(sel.step() == ir::SqliteStatement::Step::Row);
    CHECK(sel.column_blob(0).empty());
}

TEST_CASE("storage.sqlite", "transaction commits on true, rolls back on false") {
    ir::SqliteDatabase db;
    REQUIRE(db.open_memory());
    make_kv_table(db);

    bool committed = db.transaction([&] {
        return db.exec("INSERT INTO kv(k,v) VALUES(1,'a');");
    });
    CHECK(committed);

    bool rolled_back = db.transaction([&] {
        db.exec("INSERT INTO kv(k,v) VALUES(2,'b');");
        return false;  // request rollback
    });
    CHECK(!rolled_back);

    ir::SqliteStatement sel = db.prepare("SELECT COUNT(*) FROM kv;");
    REQUIRE(sel.step() == ir::SqliteStatement::Step::Row);
    CHECK_EQ(sel.column_int64(0), 1);  // only the committed row survives
}

TEST_CASE("storage.sqlite", "transaction rolls back and rethrows on exception") {
    ir::SqliteDatabase db;
    REQUIRE(db.open_memory());
    make_kv_table(db);

    bool threw = false;
    try {
        db.transaction([&]() -> bool {
            db.exec("INSERT INTO kv(k,v) VALUES(9,'x');");
            throw std::runtime_error("boom");
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    ir::SqliteStatement sel = db.prepare("SELECT COUNT(*) FROM kv;");
    REQUIRE(sel.step() == ir::SqliteStatement::Step::Row);
    CHECK_EQ(sel.column_int64(0), 0);  // the insert was rolled back
}

TEST_CASE("storage.sqlite", "invalid SQL yields an invalid statement and an error") {
    ir::SqliteDatabase db;
    REQUIRE(db.open_memory());
    ir::SqliteStatement bad = db.prepare("SELECT FROM WHERE nonsense;");
    CHECK(!bad.valid());
    CHECK(!db.last_error().empty());
}

TEST_CASE("storage.sqlite", "on-disk database persists across reopen") {
    irtest::TempDbFile tmp;
    {
        ir::SqliteDatabase db;
        REQUIRE(db.open(tmp.path));
        make_kv_table(db);
        CHECK(db.exec("INSERT INTO kv(k,v) VALUES(7,'persist');"));
    }
    {
        ir::SqliteDatabase db;
        REQUIRE(db.open(tmp.path));
        ir::SqliteStatement sel = db.prepare("SELECT v FROM kv WHERE k=7;");
        REQUIRE(sel.step() == ir::SqliteStatement::Step::Row);
        CHECK_EQ(sel.column_text(0), std::string("persist"));
    }
}

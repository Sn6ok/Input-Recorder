#include "storage/sqlite_database.hpp"

#include "sqlite3.h"

namespace ir {

// ---- SqliteStatement -----------------------------------------------------

SqliteStatement::~SqliteStatement() {
    if (stmt_ != nullptr) sqlite3_finalize(stmt_);
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : db_(other.db_), stmt_(other.stmt_) {
    other.db_ = nullptr;
    other.stmt_ = nullptr;
}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) sqlite3_finalize(stmt_);
        db_ = other.db_;
        stmt_ = other.stmt_;
        other.db_ = nullptr;
        other.stmt_ = nullptr;
    }
    return *this;
}

void SqliteStatement::bind_int64(int index, std::int64_t value) {
    sqlite3_bind_int64(stmt_, index, value);
}

void SqliteStatement::bind_text(int index, std::string_view value) {
    sqlite3_bind_text(stmt_, index, value.data(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void SqliteStatement::bind_blob(int index, const std::vector<std::byte>& value) {
    // A zero-length blob must still bind as a (non-null) empty blob.
    sqlite3_bind_blob(stmt_, index, value.data(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void SqliteStatement::bind_null(int index) { sqlite3_bind_null(stmt_, index); }

SqliteStatement::Step SqliteStatement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return Step::Row;
    if (rc == SQLITE_DONE) return Step::Done;
    return Step::Error;
}

std::int64_t SqliteStatement::column_int64(int col) const {
    return sqlite3_column_int64(stmt_, col);
}

std::string SqliteStatement::column_text(int col) const {
    const auto* text = sqlite3_column_text(stmt_, col);
    const int bytes = sqlite3_column_bytes(stmt_, col);
    if (text == nullptr || bytes <= 0) return {};
    return std::string(reinterpret_cast<const char*>(text),
                       static_cast<std::size_t>(bytes));
}

std::vector<std::byte> SqliteStatement::column_blob(int col) const {
    const void* data = sqlite3_column_blob(stmt_, col);
    const int bytes = sqlite3_column_bytes(stmt_, col);
    if (data == nullptr || bytes <= 0) return {};
    const auto* p = static_cast<const std::byte*>(data);
    return std::vector<std::byte>(p, p + bytes);
}

bool SqliteStatement::column_is_null(int col) const {
    return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

void SqliteStatement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// ---- SqliteDatabase ------------------------------------------------------

SqliteDatabase::~SqliteDatabase() { close(); }

SqliteDatabase::SqliteDatabase(SqliteDatabase&& other) noexcept
    : db_(other.db_), last_error_(std::move(other.last_error_)) {
    other.db_ = nullptr;
}

SqliteDatabase& SqliteDatabase::operator=(SqliteDatabase&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        last_error_ = std::move(other.last_error_);
        other.db_ = nullptr;
    }
    return *this;
}

void SqliteDatabase::capture_error() {
    last_error_ = db_ != nullptr ? sqlite3_errmsg(db_) : "database not open";
}

bool SqliteDatabase::open(const std::string& path) {
    close();
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        capture_error();
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    return true;
}

bool SqliteDatabase::open_memory() { return open(":memory:"); }

void SqliteDatabase::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteDatabase::exec(std::string_view sql) {
    if (db_ == nullptr) {
        last_error_ = "database not open";
        return false;
    }
    char* err = nullptr;
    const std::string sql_str(sql);
    if (sqlite3_exec(db_, sql_str.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        last_error_ = err != nullptr ? err : "exec failed";
        if (err != nullptr) sqlite3_free(err);
        return false;
    }
    return true;
}

SqliteStatement SqliteDatabase::prepare(std::string_view sql) {
    if (db_ == nullptr) {
        last_error_ = "database not open";
        return SqliteStatement{};
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt,
                           nullptr) != SQLITE_OK) {
        capture_error();
        return SqliteStatement{};
    }
    return SqliteStatement{db_, stmt};
}

std::int64_t SqliteDatabase::last_insert_rowid() const {
    return db_ != nullptr ? sqlite3_last_insert_rowid(db_) : 0;
}

int SqliteDatabase::changes() const {
    return db_ != nullptr ? sqlite3_changes(db_) : 0;
}

bool SqliteDatabase::begin() { return exec("BEGIN IMMEDIATE;"); }
bool SqliteDatabase::commit() { return exec("COMMIT;"); }
bool SqliteDatabase::rollback() { return exec("ROLLBACK;"); }

}  // namespace ir

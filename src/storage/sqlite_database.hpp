#pragma once

// Thin RAII wrapper over the SQLite C API (spec §169, §172). Keeps the rest of
// the storage layer free of raw sqlite3* handles and manual finalize/close, and
// centralises error handling, prepared statements and transactions.
//
// This is OS-free (portable C library) and is unit-tested on any platform,
// including against on-disk temp databases and ":memory:".

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ir {

class SqliteDatabase;

// A prepared statement. Move-only; finalises on destruction.
class SqliteStatement {
public:
    enum class Step { Row, Done, Error };

    SqliteStatement() = default;
    ~SqliteStatement();
    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;
    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    bool valid() const { return stmt_ != nullptr; }

    // 1-based bind indices (SQLite convention).
    void bind_int64(int index, std::int64_t value);
    void bind_text(int index, std::string_view value);
    void bind_blob(int index, const std::vector<std::byte>& value);
    void bind_null(int index);

    Step step();

    // 0-based column accessors.
    std::int64_t column_int64(int col) const;
    std::string column_text(int col) const;
    std::vector<std::byte> column_blob(int col) const;
    bool column_is_null(int col) const;

    void reset();  // reuse the statement for another execution

private:
    friend class SqliteDatabase;
    SqliteStatement(sqlite3* db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}

    sqlite3* db_ = nullptr;      // not owned
    sqlite3_stmt* stmt_ = nullptr;
};

class SqliteDatabase {
public:
    SqliteDatabase() = default;
    ~SqliteDatabase();
    SqliteDatabase(SqliteDatabase&&) noexcept;
    SqliteDatabase& operator=(SqliteDatabase&&) noexcept;
    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    // Opens (creating if needed) a database file. Returns false on failure.
    bool open(const std::string& path);
    // Opens a private in-memory database (tests).
    bool open_memory();
    void close();

    bool is_open() const { return db_ != nullptr; }

    // Runs one or more semicolon-separated statements with no result rows
    // (PRAGMAs, DDL). Returns false on error.
    bool exec(std::string_view sql);

    // Prepares a single statement. On failure returns an invalid statement and
    // sets last_error().
    SqliteStatement prepare(std::string_view sql);

    std::int64_t last_insert_rowid() const;
    int changes() const;
    const std::string& last_error() const { return last_error_; }

    // Convenience: BEGIN IMMEDIATE; body(); COMMIT — or ROLLBACK if body returns
    // false or throws. Returns body()'s result (false if the transaction could
    // not be started or committed).
    template <typename Fn>
    bool transaction(Fn&& body) {
        if (!begin()) return false;
        bool ok = false;
        try {
            ok = body();
        } catch (...) {
            rollback();
            throw;
        }
        if (ok) {
            if (commit()) return true;
            rollback();
            return false;
        }
        rollback();
        return false;
    }

    bool begin();
    bool commit();
    bool rollback();

    sqlite3* handle() { return db_; }

private:
    void capture_error();

    sqlite3* db_ = nullptr;
    std::string last_error_;
};

}  // namespace ir

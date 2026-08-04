// sweep.db results store — SQLite backing for 03 §7 (D8) — M5-T2.

#include "core/store/store.h"

#include <sqlite3.h>

#include <string>
#include <vector>

namespace ns::store {

namespace {

// Run `sql` (which may contain several statements) for its side effects; throw on
// any error, carrying sqlite3's message.
void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "sqlite3_exec failed";
        sqlite3_free(err);
        throw StoreError(msg);
    }
}

// RAII around a prepared statement: prepare in the ctor, finalize in the dtor.
// All parameter binding uses this (never string concatenation) so arbitrary JSON
// in params_json / tally_json — quotes, braces, embedded bytes — can never break
// the query or inject SQL.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            throw StoreError(std::string("sqlite3_prepare_v2 failed: ") + sqlite3_errmsg(db));
        }
    }
    ~Stmt() {
        if (stmt_) sqlite3_finalize(stmt_);
    }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    void bind_text(int idx, const std::string& v) {
        // SQLITE_TRANSIENT: sqlite copies the bytes, so `v`'s lifetime does not
        // have to outlive the step.
        if (sqlite3_bind_text(stmt_, idx, v.c_str(), static_cast<int>(v.size()),
                              SQLITE_TRANSIENT) != SQLITE_OK) {
            throw StoreError(std::string("sqlite3_bind_text failed: ") + sqlite3_errmsg(db_));
        }
    }
    void bind_double(int idx, double v) {
        if (sqlite3_bind_double(stmt_, idx, v) != SQLITE_OK) {
            throw StoreError(std::string("sqlite3_bind_double failed: ") + sqlite3_errmsg(db_));
        }
    }
    void bind_int64(int idx, std::int64_t v) {
        if (sqlite3_bind_int64(stmt_, idx, v) != SQLITE_OK) {
            throw StoreError(std::string("sqlite3_bind_int64 failed: ") + sqlite3_errmsg(db_));
        }
    }

    // Advance one row. Returns SQLITE_ROW, SQLITE_DONE, or throws on error.
    int step() {
        int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            throw StoreError(std::string("sqlite3_step failed: ") + sqlite3_errmsg(db_));
        }
        return rc;
    }

    std::string col_text(int i) const {
        const unsigned char* p = sqlite3_column_text(stmt_, i);
        if (!p) return {};
        return std::string(reinterpret_cast<const char*>(p),
                           static_cast<std::size_t>(sqlite3_column_bytes(stmt_, i)));
    }
    double col_double(int i) const { return sqlite3_column_double(stmt_, i); }
    std::int64_t col_int64(int i) const { return sqlite3_column_int64(stmt_, i); }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace

SweepStore::SweepStore(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        // sqlite3_open allocates a handle even on failure so the message is
        // readable; close it (safe on nullptr) before throwing.
        std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open failed";
        sqlite3_close(db_);
        db_ = nullptr;
        throw StoreError("sqlite3_open('" + path + "'): " + msg);
    }
    // A concurrent worker (06 §2) should wait for a busy database, not error out.
    sqlite3_busy_timeout(db_, 5000);
    // WAL lets readers and one writer proceed concurrently (multiple nukefarm
    // workers on one sweep.db); it is a no-op on an in-memory database.
    exec(db_, "PRAGMA journal_mode=WAL;");
    // 03 §7 schema, verbatim. IF NOT EXISTS so reopening an existing sweep.db is a
    // no-op. TEXT PRIMARY KEY on unit_id is what makes record_run idempotent.
    exec(db_,
         "CREATE TABLE IF NOT EXISTS runs ("
         "  unit_id TEXT PRIMARY KEY,"
         "  params_json TEXT,"
         "  tally_json TEXT,"
         "  wall_s REAL,"
         "  status TEXT"
         ");"
         "CREATE TABLE IF NOT EXISTS cursor ("
         "  state_json TEXT"
         ");");
}

SweepStore::~SweepStore() {
    // Safe on nullptr; finalizes nothing outstanding because every Stmt is scoped.
    sqlite3_close(db_);
}

bool SweepStore::record_run(const RunRecord& r) {
    // INSERT OR IGNORE: a pre-existing row for this unit_id is left untouched and
    // no error is raised — that IS the idempotency (D6). sqlite3_changes then
    // reports 1 for a fresh insert, 0 for the ignored (already-present) case.
    Stmt s(db_,
           "INSERT OR IGNORE INTO runs (unit_id, params_json, tally_json, wall_s, status) "
           "VALUES (?, ?, ?, ?, ?);");
    s.bind_text(1, r.unit_id);
    s.bind_text(2, r.params_json);
    s.bind_text(3, r.tally_json);
    s.bind_double(4, r.wall_s);
    s.bind_text(5, r.status);
    s.step();
    return sqlite3_changes(db_) == 1;
}

std::optional<RunRecord> SweepStore::get_run(const std::string& unit_id) const {
    Stmt s(db_,
           "SELECT unit_id, params_json, tally_json, wall_s, status "
           "FROM runs WHERE unit_id = ?;");
    s.bind_text(1, unit_id);
    if (s.step() == SQLITE_DONE) return std::nullopt;
    RunRecord r;
    r.unit_id = s.col_text(0);
    r.params_json = s.col_text(1);
    r.tally_json = s.col_text(2);
    r.wall_s = s.col_double(3);
    r.status = s.col_text(4);
    return r;
}

std::optional<std::string> SweepStore::status_of(const std::string& unit_id) const {
    Stmt s(db_, "SELECT status FROM runs WHERE unit_id = ?;");
    s.bind_text(1, unit_id);
    if (s.step() == SQLITE_DONE) return std::nullopt;
    return s.col_text(0);
}

bool SweepStore::is_done(const std::string& unit_id) const {
    std::optional<std::string> st = status_of(unit_id);
    return st.has_value() && *st == status::kDone;
}

std::int64_t SweepStore::count() const {
    Stmt s(db_, "SELECT COUNT(*) FROM runs;");
    s.step();
    return s.col_int64(0);
}

std::int64_t SweepStore::count_with_status(const std::string& status) const {
    Stmt s(db_, "SELECT COUNT(*) FROM runs WHERE status = ?;");
    s.bind_text(1, status);
    s.step();
    return s.col_int64(0);
}

std::vector<double> SweepStore::recent_wall_s(std::int64_t limit) const {
    Stmt s(db_, "SELECT wall_s FROM runs WHERE status = ? ORDER BY rowid DESC LIMIT ?;");
    s.bind_text(1, std::string(status::kDone));
    s.bind_int64(2, limit);
    std::vector<double> out;
    while (s.step() == SQLITE_ROW) out.push_back(s.col_double(0));
    return out;
}

void SweepStore::set_cursor(const std::string& state_json) {
    // cursor(state_json) is a singleton table: clear then insert, atomically, so a
    // reader never sees zero or two rows.
    exec(db_, "BEGIN;");
    try {
        exec(db_, "DELETE FROM cursor;");
        Stmt s(db_, "INSERT INTO cursor (state_json) VALUES (?);");
        s.bind_text(1, state_json);
        s.step();
        exec(db_, "COMMIT;");
    } catch (...) {
        // Best-effort rollback; ignore its own failure and rethrow the original.
        char* err = nullptr;
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &err);
        sqlite3_free(err);
        throw;
    }
}

std::optional<std::string> SweepStore::get_cursor() const {
    Stmt s(db_, "SELECT state_json FROM cursor LIMIT 1;");
    if (s.step() == SQLITE_DONE) return std::nullopt;
    return s.col_text(0);
}

}  // namespace ns::store

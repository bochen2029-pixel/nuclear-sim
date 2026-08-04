// sweep.db — the batch results store (03 §7, D8) — M5-T2.
//
// The SQLite-backed artifact store `nukefarm` (06 §2) records sweep results into:
//   runs(unit_id PK, params_json, tally_json, wall_s, status)
//   cursor(state_json)
// The `unit_id` (03 §6, ns::api::compute_unit_id) is the idempotent dedup key:
// recording a unit that is already present is a NO-OP (the "second finisher's
// write is a no-op" / D6 double-count fix), so a resumed sweep never double-counts
// and rerunning a completed unit is a no-op by construction.
//
// LAYERING (SYNC-M5b): the store keys on `unit_id` as an opaque STRING, so this
// module depends only on sqlite3 — NOT on src/api (where compute_unit_id lives).
// The caller (api / nukefarm) computes the unit_id and hands the string down. This
// is the M5-T1 discipline: the core container is generic; the owning layer supplies
// the semantics. It is nscore infrastructure, not a frontend (D7 keeps frontends
// physics-free; the persistence library lives in core, the thin frontend calls it).
//
// SCOPE (M5-T2): the store LIBRARY. The `nukefarm` CLI, the filesystem work-queue,
// and the samplers (06 §2) are the sweep ENGINE (M5-T3/T4). The per-run artifact
// bundle emission (artifacts/<unit_id>/{run.json,tally.json,checkpoint.bin}, 03 §6)
// is the run-emitter's job.

#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// sqlite3 is an opaque handle here; <sqlite3.h> is an implementation detail (.cpp).
struct sqlite3;

namespace ns::store {

/// Thrown on any SQLite failure (open, schema, prepare, bind, step), carrying the
/// underlying sqlite3 diagnostic.
struct StoreError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// The 03 §7 `status` vocabulary. These are a schema enum, NOT physical constants
/// (so they do not live in ns::consts): the results DB shares them with the 06 §2
/// filesystem queue (pending / claimed→running / done). The store persists whatever
/// status string it is handed and treats "done" as the terminal state its dedup
/// predicate (`is_done`) tests; the queue lifecycle itself is M5-T3.
namespace status {
inline constexpr char kPending[] = "pending";
inline constexpr char kRunning[] = "running";
inline constexpr char kDone[] = "done";
inline constexpr char kError[] = "error";
}  // namespace status

/// One row of the 03 §7 `runs` table.
struct RunRecord {
    std::string unit_id;      ///< PK — the 03 §6 dedup key (ns::api::compute_unit_id)
    std::string params_json;  ///< the applied sweep-point params (03 §7)
    std::string tally_json;   ///< 03 §5 tally.json for this run
    double wall_s = 0.0;      ///< wall-clock seconds for the run
    std::string status = status::kDone;
};

/// The 03 §7 `sweep.db` results store. Opens (creating if absent) a SQLite database
/// and ensures the `runs` + `cursor` schema. Owns the sqlite3 handle; non-copyable
/// AND non-movable (a hand-written move would risk a double-close — callers that
/// need to relocate one hold it by std::unique_ptr).
class SweepStore {
public:
    /// Open the store at `path`, creating the file + schema if absent.
    /// `path == ":memory:"` opens an ephemeral in-process database. Throws
    /// StoreError if the database cannot be opened or the schema created.
    explicit SweepStore(const std::string& path);
    ~SweepStore();
    SweepStore(const SweepStore&) = delete;
    SweepStore& operator=(const SweepStore&) = delete;
    SweepStore(SweepStore&&) = delete;
    SweepStore& operator=(SweepStore&&) = delete;

    /// Idempotent record (03 §7 / D6). Inserts the row for `r.unit_id`. If a row
    /// with that unit_id already exists this is a NO-OP — nothing is overwritten —
    /// and it returns false (the losing "second finisher"). Returns true iff the row
    /// was newly inserted. Rerunning a recorded unit is thus a no-op.
    bool record_run(const RunRecord& r);

    /// The full row for `unit_id`, or nullopt if absent.
    std::optional<RunRecord> get_run(const std::string& unit_id) const;

    /// The dedup predicate a sweep consults before scheduling a unit: true iff a row
    /// exists for `unit_id` with status == "done".
    bool is_done(const std::string& unit_id) const;

    /// The recorded status for `unit_id`, or nullopt if the unit is absent.
    std::optional<std::string> status_of(const std::string& unit_id) const;

    /// Number of rows in `runs`.
    std::int64_t count() const;
    /// Number of rows in `runs` whose status == `status`.
    std::int64_t count_with_status(const std::string& status) const;

    /// The `wall_s` of the most-recently-recorded `done` runs, newest first, up to
    /// `limit`. A sweep driver uses this to estimate the per-unit runtime (06 §2 /
    /// ADR-020 stale-lease policy).
    std::vector<double> recent_wall_s(std::int64_t limit) const;

    /// Sweep cursor (03 §7 `cursor(state_json)`; checkpoint section 8) — the single
    /// resumable sweep-progress blob. `set_cursor` replaces it (atomically);
    /// `get_cursor` is nullopt until first set.
    void set_cursor(const std::string& state_json);
    std::optional<std::string> get_cursor() const;

private:
    sqlite3* db_ = nullptr;
};

}  // namespace ns::store

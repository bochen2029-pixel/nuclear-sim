// The filesystem work-queue for distributed sweeps (06 §2, D8) — M5-T3-c.
//
// A crash-safe, resumable queue of sweep units on the filesystem:
//   <dir>/pending/<unit_id>.json                     -- waiting to run (its payload)
//   <dir>/claimed/<unit_id>.json  + <unit_id>.lease  -- claimed by a worker
//   <dir>/done/<unit_id>.json                        -- completed
// The `unit_id` (03 §6) is the key: a requeued or double-claimed unit lands at the
// same path, so a second finisher's write to the artifact store is a no-op (the
// store's INSERT-OR-IGNORE record_run, M5-T2 / D6). Coordination between workers is
// the atomicity of filesystem renames — no lock server.
//
// This LIBRARY is policy-free: the stale-lease THRESHOLD is a caller parameter, and
// time (`now_s`) is passed in (deterministic + testable). The worker loop, the
// submit/worker/status/resume CLI, and the threshold policy (2x median runtime,
// 06 §2) are M5-T3-d.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace ns::nukefarm {

struct QueueError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// A claimed unit: its id + the payload it was enqueued with (opaque to the queue —
/// the sweep stores the ParamSet JSON here).
struct WorkItem {
    std::string unit_id;
    std::string payload;
};

/// The 06 §2 filesystem work-queue. Holds only the root path, so several instances
/// may share one directory — that IS the multi-worker model.
class WorkQueue {
public:
    /// Open the queue at `dir`, creating pending/claimed/done under it.
    explicit WorkQueue(std::filesystem::path dir);

    /// Enqueue a unit. Idempotent by `unit_id`: a unit already claimed or done is
    /// NOT re-added; a still-pending unit has its payload overwritten. Returns true
    /// iff a new pending entry was created.
    bool enqueue(const std::string& unit_id, const std::string& payload);

    /// Claim one pending unit: atomically move it to claimed/ and write its lease
    /// stamped `now_s`. Returns the item, or nullopt if nothing is pending.
    std::optional<WorkItem> claim(double now_s);

    /// Mark a claimed unit done: move it to done/ and drop its lease. Throws if the
    /// unit is not currently claimed.
    void complete(const std::string& unit_id);

    /// Requeue every claimed unit whose lease age (`now_s` − lease stamp) exceeds
    /// `threshold_s` — a crashed or slow worker — moving it back to pending and
    /// dropping the lease. Returns the count reclaimed. The threshold POLICY is the
    /// caller's (M5-T3-d); an unparseable lease is treated as stale.
    int reclaim_stale(double now_s, double threshold_s);

    std::int64_t pending_count() const;
    std::int64_t claimed_count() const;
    std::int64_t done_count() const;

private:
    std::filesystem::path dir_;
    std::filesystem::path pending_;
    std::filesystem::path claimed_;
    std::filesystem::path done_;
};

}  // namespace ns::nukefarm

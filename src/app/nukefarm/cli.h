// The nukefarm CLI subcommand handlers (06 §2) — M5-T3-e.
//
// Thin, testable handlers that open a WorkQueue + SweepStore from paths and drive
// the M5-T3-a..-d functions (submit / run_worker / stale_threshold_s). The
// `nukefarm` exe (main.cpp) parses CLI11 args and dispatches here with the default
// evaluator + a real wall-clock; tests call these directly with a stub evaluator.

#pragma once

#include "app/nukefarm/runner.h"  // Evaluator, default_evaluator
#include "app/nukefarm/worker.h"  // WorkerResult

#include <cstdint>
#include <string>

namespace ns::nukefarm {

/// A wall-clock reading in seconds (system_clock epoch). The single clock source
/// fed into the worker's injected-clock seam in production.
double wall_clock_seconds();

/// `submit`: load `sweep_toml`, enqueue its sampled points into the queue at
/// `queue_dir`, skipping units already `done` in the store at `db`. Returns the
/// number newly enqueued.
std::int64_t cli_submit(const std::string& sweep_toml, const std::string& queue_dir,
                        const std::string& db);

/// `worker`: drain the queue at `queue_dir` into the store at `db`, reclaiming
/// leases stale by `stale_threshold_s(store, stale_lease_s)`. `eval` defaults to the
/// real demon-core `generate_run` (tests inject a stub).
WorkerResult cli_worker(const std::string& queue_dir, const std::string& db, double stale_lease_s,
                        Evaluator eval = default_evaluator);

/// `status` counts for a sweep.
struct StatusReport {
    std::int64_t store_done = 0;
    std::int64_t queue_pending = 0;
    std::int64_t queue_claimed = 0;
    std::int64_t queue_done = 0;
};

/// `status`: the store's `done` count + (when `queue_dir` is non-empty) the queue
/// pending/claimed/done counts.
StatusReport cli_status(const std::string& db, const std::string& queue_dir);

}  // namespace ns::nukefarm

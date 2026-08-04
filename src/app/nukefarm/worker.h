// The distributed sweep worker: submit + drain + the stale-lease policy
// (06 §2, ADR-020) — M5-T3-d.
//
// `submit` enqueues a manifest's sampled points into the WorkQueue (keyed by
// studio_unit_id). `run_worker` drains the queue — reclaim stale leases, then
// claim → evaluate → record_run → complete — so several workers over one queue
// (06 §2) share the load, and a reclaimed unit run twice is not double-counted
// (the store's INSERT-OR-IGNORE record_run, M5-T2). `stale_threshold_s` is the
// 06 §2 / ADR-020 policy.
//
// This is the distributed-sweep CORE (testable via functions + an injected
// clock/evaluator). The `nukefarm` CLI exe (submit/worker/status/resume) is
// M5-T3-e.

#pragma once

#include "app/nukefarm/queue.h"
#include "app/nukefarm/runner.h"
#include "app/nukefarm/sweep.h"
#include "core/store/store.h"

#include <cstdint>
#include <functional>

namespace ns::nukefarm {

/// The stale-lease requeue threshold (06 §2 / ADR-020): 2× the median of the last
/// 10 completed wall-clock runtimes; the wall-clock `fallback_s` (default 600 s ≈
/// 10 min — NOT `t_max_s`, which is sim time) when fewer than 10 have completed.
double stale_threshold_s(const ns::store::SweepStore& store, double fallback_s = 600.0);

/// Enqueue every sampled point of `manifest` into `queue`, keyed by
/// `studio_unit_id`, with the point's override JSON as the payload. A unit already
/// `done` in `store` is skipped (resume). Returns the number newly enqueued.
std::int64_t submit(const SweepManifest& manifest, WorkQueue& queue,
                    const ns::store::SweepStore& store);

/// The outcome of one `run_worker` drain.
struct WorkerResult {
    int processed = 0;   ///< units evaluated + recorded this call
    int reclaimed = 0;   ///< stale leases requeued before draining
    int skipped = 0;     ///< claimed but already `done` (completed without re-running)
};

/// Drain `queue`: reclaim leases older than `threshold_s` at `clock()`, then
/// repeatedly claim → (skip if already done) → evaluate → `record_run` → complete
/// until nothing is pending. `clock` supplies wall-clock seconds (injected for
/// tests). `record_run` is idempotent, so a reclaimed unit is never double-counted.
WorkerResult run_worker(WorkQueue& queue, ns::store::SweepStore& store, const Evaluator& eval,
                        const std::function<double()>& clock, double threshold_s);

}  // namespace ns::nukefarm

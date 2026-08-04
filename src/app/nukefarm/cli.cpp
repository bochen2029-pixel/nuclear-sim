// The nukefarm CLI subcommand handlers (06 §2) — M5-T3-e.

#include "app/nukefarm/cli.h"

#include "app/nukefarm/queue.h"
#include "app/nukefarm/sweep.h"
#include "core/store/store.h"

#include <chrono>

namespace ns::nukefarm {

double wall_clock_seconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t cli_submit(const std::string& sweep_toml, const std::string& queue_dir,
                        const std::string& db) {
    const SweepManifest manifest = SweepManifest::load(sweep_toml);
    WorkQueue queue(queue_dir);
    ns::store::SweepStore store(db);
    return submit(manifest, queue, store);
}

WorkerResult cli_worker(const std::string& queue_dir, const std::string& db, double stale_lease_s,
                        Evaluator eval) {
    WorkQueue queue(queue_dir);
    ns::store::SweepStore store(db);
    const double threshold = stale_threshold_s(store, stale_lease_s);
    return run_worker(queue, store, eval, wall_clock_seconds, threshold);
}

StatusReport cli_status(const std::string& db, const std::string& queue_dir) {
    ns::store::SweepStore store(db);
    StatusReport r;
    r.store_done = store.count_with_status(ns::store::status::kDone);
    if (!queue_dir.empty()) {
        WorkQueue queue(queue_dir);
        r.queue_pending = queue.pending_count();
        r.queue_claimed = queue.claimed_count();
        r.queue_done = queue.done_count();
    }
    return r;
}

}  // namespace ns::nukefarm

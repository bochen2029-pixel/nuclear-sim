// The distributed sweep worker (06 §2, ADR-020) — M5-T3-d.

#include "app/nukefarm/worker.h"

#include "api/studio.h"
#include "physics/tally/tally.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ns::nukefarm {

double stale_threshold_s(const ns::store::SweepStore& store, double fallback_s) {
    std::vector<double> w = store.recent_wall_s(10);
    if (w.size() < 10) return fallback_s;  // ADR-020: bootstrap on a wall-clock default
    std::sort(w.begin(), w.end());
    const double median = (w[4] + w[5]) / 2.0;  // median of exactly 10
    return 2.0 * median;
}

std::int64_t submit(const SweepManifest& manifest, WorkQueue& queue,
                    const ns::store::SweepStore& store) {
    std::unique_ptr<Sampler> sampler = make_sampler(manifest);
    std::int64_t enqueued = 0;
    while (std::optional<ParamSet> point = sampler->next()) {
        const std::string payload = param_json(*point);
        const ns::api::StudioConfig cfg = ns::api::StudioConfig::from_json(payload);
        const std::string unit_id = ns::api::studio_unit_id(cfg);
        if (store.is_done(unit_id)) continue;  // resume: already complete, don't re-enqueue
        if (queue.enqueue(unit_id, payload)) ++enqueued;
    }
    return enqueued;
}

WorkerResult run_worker(WorkQueue& queue, ns::store::SweepStore& store, const Evaluator& eval,
                        const std::function<double()>& clock, double threshold_s) {
    WorkerResult r;
    // Reclaim leases orphaned by crashed/slow workers before draining what's ready.
    r.reclaimed = queue.reclaim_stale(clock(), threshold_s);

    while (std::optional<WorkItem> item = queue.claim(clock())) {
        if (store.is_done(item->unit_id)) {  // finished elsewhere -> just clear the queue entry
            queue.complete(item->unit_id);
            ++r.skipped;
            continue;
        }
        const ns::api::StudioConfig cfg = ns::api::StudioConfig::from_json(item->payload);
        const RunOutcome out = eval(cfg);
        // INSERT-OR-IGNORE: even a reclaimed unit run by two workers records once.
        store.record_run({item->unit_id, item->payload, ns::physics::to_json(out.tally), out.wall_s,
                          std::string(ns::store::status::kDone)});
        queue.complete(item->unit_id);
        ++r.processed;
    }
    return r;
}

}  // namespace ns::nukefarm

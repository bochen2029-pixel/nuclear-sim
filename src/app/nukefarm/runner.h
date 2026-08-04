// The store-backed sweep run-loop engine (06 §2, D8) — M5-T3-b.
//
// `run_sweep` drives a SweepManifest's sampled points (M5-T3-a `make_sampler`)
// through an evaluator into the M5-T2 SweepStore, keyed by the demon-core
// `unit_id` (ns::api::studio_unit_id): `is_done` SKIPS completed units (resume),
// and `record_run` is INSERT-OR-IGNORE (the no-double-count backstop). The
// evaluator is injectable — the fast4-independent `generate_run` for real, a fast
// stub for tests (the M3-T3-b injectable pattern).
//
// SCOPE (M5-T3-b): the LOCAL run loop — the DoD's "100-run local sweep completes;
// resume skips done units". The FS work-queue + lease/reclaim + the distributed
// `worker` + the submit/worker/status/resume CLI are M5-T3-c.

#pragma once

#include "api/studio.h"
#include "app/nukefarm/sweep.h"
#include "core/store/store.h"
#include "physics/tally/tally.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ns::nukefarm {

/// What the runner records for one evaluated point.
struct RunOutcome {
    ns::physics::TallyResult tally;  ///< 03 §5 — serialized into the store's tally_json
    std::string run_json;            ///< 03 §6 run.json ("" if the evaluator has none)
    double wall_s = 0.0;             ///< wall-clock seconds for the run
};

/// cfg -> outcome. Injectable: `default_evaluator` wraps the real `generate_run`;
/// tests inject a fast deterministic stub.
using Evaluator = std::function<RunOutcome(const ns::api::StudioConfig&)>;

/// The real evaluator: run the fast4-independent demon-core `generate_run` and
/// time it. Each call runs a Monte-Carlo burst (seconds).
RunOutcome default_evaluator(const ns::api::StudioConfig& cfg);

/// The flat dotted-key override JSON for one ParamSet (`{"compression.ratio": 2.3,
/// ...}`) — what `StudioConfig::from_json` reads and what a queued unit's payload
/// carries (M5-T3-d submit/worker).
std::string param_json(const ParamSet& point);

/// Map one ParamSet onto a StudioConfig, starting from the default demon-core cfg,
/// via the flat dotted keys `StudioConfig::from_json` understands
/// ("compression.ratio", "pit.mass_kg", "materials.pu_ga_delta.Pu240", ...). A
/// param that is not a MODELLED demon-core key is ignored (from_json's contract);
/// sweep over the modelled axes. (A non-default / scenario-file base is the
/// fast4-gated full-device path, not M5-T3-b.)
ns::api::StudioConfig apply_point(const ParamSet& point);

/// Summary of one run_sweep invocation.
struct SweepProgress {
    std::int64_t total = 0;    ///< points the sampler produced
    std::int64_t ran = 0;      ///< evaluated this invocation
    std::int64_t skipped = 0;  ///< already `done` in the store (resume skip)
};

/// Run every sampled point of `manifest` through `eval` into `store`, keyed by
/// `studio_unit_id`. A unit already `done` is SKIPPED (resume); each completed
/// unit is recorded once (idempotent — no double-count even if re-scheduled).
/// Writes the store cursor every `manifest.checkpoint_every_runs` completed runs.
/// Re-invoking with the same store resumes: only the not-yet-done units run.
SweepProgress run_sweep(const SweepManifest& manifest, ns::store::SweepStore& store,
                        const Evaluator& eval);

}  // namespace ns::nukefarm

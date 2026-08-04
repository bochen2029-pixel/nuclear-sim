// The store-backed sweep run-loop engine (06 §2) — M5-T3-b.

#include "app/nukefarm/runner.h"

#include "api/run_provenance.h"  // ns::api::to_json(RunProvenance)

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>

namespace ns::nukefarm {

std::string param_json(const ParamSet& point) {
    // A flat-dotted-key object; from_json maps it onto the demon-core defaults
    // (missing keys keep their default; a fixed seed means distinct params ->
    // distinct cfg -> distinct unit_id).
    nlohmann::json ov = nlohmann::json::object();
    for (const auto& a : point) ov[a.param] = a.value;
    return ov.dump();
}

ns::api::StudioConfig apply_point(const ParamSet& point) {
    return ns::api::StudioConfig::from_json(param_json(point));
}

RunOutcome default_evaluator(const ns::api::StudioConfig& cfg) {
    const auto t0 = std::chrono::steady_clock::now();
    const ns::api::GenerateRunResult res = ns::api::generate_run(cfg);
    const auto t1 = std::chrono::steady_clock::now();
    RunOutcome out;
    out.tally = res.tally;
    out.run_json = ns::api::to_json(res.run);
    out.wall_s = std::chrono::duration<double>(t1 - t0).count();
    return out;
}

SweepProgress run_sweep(const SweepManifest& manifest, ns::store::SweepStore& store,
                        const Evaluator& eval) {
    std::unique_ptr<Sampler> sampler = make_sampler(manifest);
    SweepProgress prog;
    std::int64_t since_ckpt = 0;

    while (std::optional<ParamSet> point = sampler->next()) {
        ++prog.total;
        const ns::api::StudioConfig cfg = apply_point(*point);
        const std::string unit_id = ns::api::studio_unit_id(cfg);

        if (store.is_done(unit_id)) {  // resume: this unit already completed
            ++prog.skipped;
            continue;
        }

        const RunOutcome out = eval(cfg);

        nlohmann::json params = nlohmann::json::object();
        for (const auto& a : *point) params[a.param] = a.value;

        // INSERT-OR-IGNORE: even if a stale-lease reclaim (M5-T3-c) ran this unit
        // twice, the second write is a no-op (D6).
        store.record_run({unit_id, params.dump(), ns::physics::to_json(out.tally), out.wall_s,
                          std::string(ns::store::status::kDone)});
        ++prog.ran;

        if (manifest.checkpoint_every_runs > 0 && ++since_ckpt >= manifest.checkpoint_every_runs) {
            since_ckpt = 0;
            const nlohmann::json cur = {
                {"sweep", manifest.name}, {"ran", prog.ran}, {"skipped", prog.skipped},
                {"total", prog.total}};
            store.set_cursor(cur.dump());
        }
    }
    return prog;
}

}  // namespace ns::nukefarm

// M5-T3-a — the 03 §7 sweep.toml loader + the axis/objective enforcement
// (MAJ-35) + the 06 §2 samplers grid/lhs/random. Catch2 names are ASCII with no
// commas (a comma or em-dash breaks the ctest<->Catch2 filter — the M5-T1 trap).

#include "app/nukefarm/sweep.h"

#include "app/nukefarm/cli.h"
#include "app/nukefarm/queue.h"
#include "app/nukefarm/runner.h"
#include "app/nukefarm/worker.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace ns::nukefarm;

namespace {

// A valid grid manifest with two numerical axes (100 runs -> a 10x10 grid).
const char* kValidGrid = R"(
schema_version = 1
name = "jitter_grid"
base_scenario = "scenarios/trinity_canonical.toml"
[sweep]
sampler = "grid"
budget_runs = 100
budget_wallclock_h = 8
checkpoint_every_runs = 50
[objective]
kind = "sensitivity"
report = ["burnup.pu_fraction", "k_eff.peak"]
[[space]]
param = "compression.ratio"
axis_class = "numerical"
range = [2.0, 2.5]
[[space]]
param = "lenses.jitter_ns"
axis_class = "numerical"
range = [0.0, 20.0]
)";

std::vector<ParamSet> collect(Sampler& s) {
    std::vector<ParamSet> v;
    while (auto p = s.next()) v.push_back(*p);
    return v;
}

bool equal_points(const std::vector<ParamSet>& a, const std::vector<ParamSet>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return false;
        for (std::size_t j = 0; j < a[i].size(); ++j) {
            if (a[i][j].param != b[i][j].param) return false;
            if (a[i][j].value != b[i][j].value) return false;  // exact: deterministic RNG
        }
    }
    return true;
}

}  // namespace

TEST_CASE("parse accepts a valid grid manifest and reads its fields", "[nukefarm]") {
    SweepManifest m = SweepManifest::parse(kValidGrid);
    REQUIRE(m.schema_version == 1);
    REQUIRE(m.name == "jitter_grid");
    REQUIRE(m.base_scenario == "scenarios/trinity_canonical.toml");
    REQUIRE(m.sampler == "grid");
    REQUIRE(m.budget_runs == 100);
    REQUIRE(m.checkpoint_every_runs == 50);
    REQUIRE(m.objective.kind == ObjectiveKind::Sensitivity);
    REQUIRE(m.objective.report.size() == 2);
    REQUIRE(m.space.size() == 2);
    REQUIRE(m.space[0].param == "compression.ratio");
    REQUIRE(m.space[0].axis_class == AxisClass::Numerical);
    REQUIRE(m.space[0].lo == 2.0);
    REQUIRE(m.space[0].hi == 2.5);
}

TEST_CASE("an unknown key hard-errors", "[nukefarm]") {
    const char* doc = R"(
name = "x"
base_scenario = "s.toml"
budgt_runs = 10
[sweep]
sampler = "grid"
budget_runs = 10
[objective]
kind = "sensitivity"
[[space]]
param = "p"
axis_class = "numerical"
range = [0.0, 1.0]
)";
    REQUIRE_THROWS_AS(SweepManifest::parse(doc), SweepError);
}

TEST_CASE("an uncertainty axis requires a constant_id", "[nukefarm]") {
    const char* doc = R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "grid"
budget_runs = 10
[objective]
kind = "sensitivity"
[[space]]
param = "compression.ratio"
axis_class = "uncertainty"
range = [2.1, 2.4]
)";
    REQUIRE_THROWS_AS(SweepManifest::parse(doc), SweepError);
}

TEST_CASE("an uncertainty range must lie inside the constant band", "[nukefarm]") {
    // C-060 = compression_ratio, band [2.0, 2.5].
    auto doc = [](const char* range) {
        return std::string(R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "grid"
budget_runs = 10
[objective]
kind = "sensitivity"
[[space]]
param = "compression.ratio"
axis_class = "uncertainty"
constant_id = "C-060"
range = )") + range + "\n";
    };
    // Inside the band -> accepted.
    REQUIRE_NOTHROW(SweepManifest::parse(doc("[2.1, 2.4]")));
    // Below the band's lo -> rejected.
    REQUIRE_THROWS_AS(SweepManifest::parse(doc("[1.9, 2.4]")), SweepError);
    // Above the band's hi -> rejected.
    REQUIRE_THROWS_AS(SweepManifest::parse(doc("[2.1, 2.7]")), SweepError);
}

TEST_CASE("an unknown constant_id is rejected", "[nukefarm]") {
    const char* doc = R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "grid"
budget_runs = 10
[objective]
kind = "sensitivity"
[[space]]
param = "p"
axis_class = "uncertainty"
constant_id = "C-000"
range = [0.0, 1.0]
)";
    REQUIRE_THROWS_AS(SweepManifest::parse(doc), SweepError);
}

TEST_CASE("a pedagogical axis is allowed only with grid sensitivity and small budget", "[nukefarm]") {
    auto doc = [](const char* sampler, int budget, const char* kind, const char* extra) {
        return std::string("name = \"x\"\nbase_scenario = \"s.toml\"\n[sweep]\nsampler = \"") +
               sampler + "\"\nbudget_runs = " + std::to_string(budget) +
               "\n[objective]\nkind = \"" + kind + "\"\n" + extra +
               "[[space]]\nparam = \"tungsten_tamper\"\naxis_class = \"pedagogical\"\nrange = [1.0, 9.0]\n";
    };
    // grid + sensitivity + budget<=100 -> accepted.
    REQUIRE_NOTHROW(SweepManifest::parse(doc("grid", 50, "sensitivity", "")));
    // random sampler with a pedagogical axis -> rejected (optimizing/space-search).
    REQUIRE_THROWS_AS(SweepManifest::parse(doc("random", 50, "sensitivity", "")), SweepError);
    // budget over 100 -> rejected.
    REQUIRE_THROWS_AS(SweepManifest::parse(doc("grid", 200, "sensitivity", "")), SweepError);
    // calibrate with a pedagogical axis -> rejected.
    REQUIRE_THROWS_AS(
        SweepManifest::parse(doc("grid", 50, "calibrate", "target_yield_kt = [16.6, 26.8]\n")),
        SweepError);
}

TEST_CASE("calibrate requires a target band and forbids a pedagogical axis", "[nukefarm]") {
    // Missing target_yield_kt -> rejected.
    const char* no_target = R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "lhs"
budget_runs = 20
[objective]
kind = "calibrate"
[[space]]
param = "compression.ratio"
axis_class = "uncertainty"
constant_id = "C-060"
range = [2.1, 2.4]
)";
    REQUIRE_THROWS_AS(SweepManifest::parse(no_target), SweepError);

    // With a target band and only uncertainty axes -> accepted.
    const char* ok = R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "lhs"
budget_runs = 20
[objective]
kind = "calibrate"
target_yield_kt = [16.6, 26.8]
[[space]]
param = "compression.ratio"
axis_class = "uncertainty"
constant_id = "C-060"
range = [2.1, 2.4]
)";
    REQUIRE_NOTHROW(SweepManifest::parse(ok));
}

TEST_CASE("an unknown sampler name is rejected", "[nukefarm]") {
    const char* doc = R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "bogus"
budget_runs = 10
[objective]
kind = "sensitivity"
[[space]]
param = "p"
axis_class = "numerical"
range = [0.0, 1.0]
)";
    REQUIRE_THROWS_AS(SweepManifest::parse(doc), SweepError);
}

TEST_CASE("the grid sampler enumerates the cartesian product within budget", "[nukefarm]") {
    SweepManifest m = SweepManifest::parse(kValidGrid);
    auto s = make_sampler(m);
    auto pts = collect(*s);

    // floor(100^(1/2)) = 10 per axis -> 100 points.
    REQUIRE(pts.size() == 100);
    for (const auto& p : pts) {
        REQUIRE(p.size() == 2);
        REQUIRE(p[0].param == "compression.ratio");
        REQUIRE(p[0].value >= 2.0);
        REQUIRE(p[0].value <= 2.5);
        REQUIRE(p[1].value >= 0.0);
        REQUIRE(p[1].value <= 20.0);
    }
    // First point is both axes at lo; last is both at hi (grid endpoints hit).
    REQUIRE(pts.front()[0].value == 2.0);
    REQUIRE(pts.front()[1].value == 0.0);
    REQUIRE(pts.back()[0].value == 2.5);
    REQUIRE(pts.back()[1].value == 20.0);
}

TEST_CASE("the random sampler is deterministic and stays in range", "[nukefarm]") {
    const char* doc = R"(
name = "rnd"
base_scenario = "s.toml"
[sweep]
sampler = "random"
budget_runs = 32
[objective]
kind = "sensitivity"
[[space]]
param = "a"
axis_class = "numerical"
range = [-1.0, 1.0]
[[space]]
param = "b"
axis_class = "numerical"
range = [10.0, 20.0]
)";
    SweepManifest m = SweepManifest::parse(doc);
    auto a = collect(*make_sampler(m));
    auto b = collect(*make_sampler(m));

    REQUIRE(a.size() == 32);
    REQUIRE(equal_points(a, b));  // same seed -> identical points
    for (const auto& p : a) {
        REQUIRE(p[0].value >= -1.0);
        REQUIRE(p[0].value < 1.0);
        REQUIRE(p[1].value >= 10.0);
        REQUIRE(p[1].value < 20.0);
    }
}

TEST_CASE("the lhs sampler is deterministic and covers every stratum", "[nukefarm]") {
    const char* doc = R"(
name = "lh"
base_scenario = "s.toml"
[sweep]
sampler = "lhs"
budget_runs = 16
[objective]
kind = "sensitivity"
[[space]]
param = "a"
axis_class = "numerical"
range = [0.0, 1.0]
[[space]]
param = "b"
axis_class = "numerical"
range = [0.0, 8.0]
)";
    SweepManifest m = SweepManifest::parse(doc);
    const int big_n = 16;
    auto a = collect(*make_sampler(m));
    auto b = collect(*make_sampler(m));

    REQUIRE(a.size() == static_cast<std::size_t>(big_n));
    REQUIRE(equal_points(a, b));

    // For each axis, the N points must hit each of the N strata exactly once.
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const double lo = m.space[axis].lo;
        const double hi = m.space[axis].hi;
        std::map<int, int> stratum_hits;
        for (const auto& p : a) {
            const double unit = (p[axis].value - lo) / (hi - lo);
            int stratum = static_cast<int>(std::floor(unit * big_n));
            if (stratum == big_n) stratum = big_n - 1;  // guard the top edge
            stratum_hits[stratum]++;
        }
        REQUIRE(stratum_hits.size() == static_cast<std::size_t>(big_n));
        for (const auto& [k, count] : stratum_hits) {
            REQUIRE(count == 1);
        }
    }
}

TEST_CASE("mcts builds from a calibrate manifest and returns a valid first point", "[nukefarm]") {
    const char* doc = R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "mcts"
budget_runs = 10
[objective]
kind = "calibrate"
target_yield_kt = [16.6, 26.8]
[[space]]
param = "compression.ratio"
axis_class = "uncertainty"
constant_id = "C-060"
range = [2.1, 2.4]
)";
    SweepManifest m = SweepManifest::parse(doc);
    auto s = make_sampler(m);  // mcts is available (M5-T4); calibrate band present
    REQUIRE(s->score_kind() == ScoreKind::BandCenter);
    auto p = s->next();
    REQUIRE(p.has_value());
    REQUIRE(p->size() == 1);
    REQUIRE(p->front().value >= 2.1);  // the first box centre lies inside the axis range
    REQUIRE(p->front().value <= 2.4);
}

// --- M5-T4: the MCTS/PUCT sampler ---

TEST_CASE("the mcts sampler converges faster than random on a synthetic landscape", "[nukefarm]") {
    // A smooth landscape peaked at (7, 3): yield equals the band centre there, so
    // BandCenter scoring (-|yield - centre|) is maximised at the optimum.
    const char* mcts_doc = R"(
name = "mcts_conv"
base_scenario = "s.toml"
[sweep]
sampler = "mcts"
budget_runs = 300
[objective]
kind = "calibrate"
target_yield_kt = [10.0, 30.0]
[[space]]
param = "a"
axis_class = "numerical"
range = [0.0, 10.0]
[[space]]
param = "b"
axis_class = "numerical"
range = [0.0, 10.0]
)";
    const double center = 20.0;  // (10 + 30) / 2
    auto landscape = [&](const ParamSet& p) {
        double x = 0.0;
        double y = 0.0;
        for (const auto& a : p) {
            if (a.param == "a") x = a.value;
            else if (a.param == "b") y = a.value;
        }
        return center - 0.1 * ((x - 7.0) * (x - 7.0) + (y - 3.0) * (y - 3.0));
    };

    SweepManifest m = SweepManifest::parse(mcts_doc);
    auto mcts = make_sampler(m);
    double mcts_best = 1e300;
    for (int i = 0; i < 300; ++i) {
        const auto p = mcts->next();
        REQUIRE(p.has_value());
        ns::physics::TallyResult t;
        t.yield_kt = landscape(*p);
        mcts->report(*p, t);
        mcts_best = std::min(mcts_best, std::abs(t.yield_kt - center));
    }

    // Random baseline over the same axes/budget (the seeded random sampler).
    const char* rnd_doc = R"(
name = "rnd_conv"
base_scenario = "s.toml"
[sweep]
sampler = "random"
budget_runs = 300
[objective]
kind = "sensitivity"
[[space]]
param = "a"
axis_class = "numerical"
range = [0.0, 10.0]
[[space]]
param = "b"
axis_class = "numerical"
range = [0.0, 10.0]
)";
    auto rnd = make_sampler(SweepManifest::parse(rnd_doc));
    double rnd_best = 1e300;
    while (const auto p = rnd->next()) {
        rnd_best = std::min(rnd_best, std::abs(landscape(*p) - center));
    }

    // MCTS refines toward the optimum; its best point is markedly closer to the
    // band centre than uniform random's after the same number of evaluations.
    REQUIRE(mcts_best < rnd_best);
}

TEST_CASE("mcts requires a calibrate objective with a target band", "[nukefarm]") {
    const char* doc = R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "mcts"
budget_runs = 10
[objective]
kind = "sensitivity"
[[space]]
param = "a"
axis_class = "numerical"
range = [0.0, 1.0]
)";
    SweepManifest m = SweepManifest::parse(doc);  // loader-valid (mcts+sensitivity+numerical)
    REQUIRE_THROWS_AS(make_sampler(m), SweepError);  // ...but BandCenter needs a target band
}

TEST_CASE("mcts with a pedagogical axis is rejected at load", "[nukefarm]") {
    const char* doc = R"(
name = "x"
base_scenario = "s.toml"
[sweep]
sampler = "mcts"
budget_runs = 10
[objective]
kind = "calibrate"
target_yield_kt = [10.0, 30.0]
[[space]]
param = "p"
axis_class = "pedagogical"
range = [1.0, 9.0]
)";
    // A pedagogical axis is permitted only with sampler=grid -> the loader rejects
    // this before mcts is ever instantiated.
    REQUIRE_THROWS_AS(SweepManifest::parse(doc), SweepError);
}

// --- M5-T3-b: the store-backed run-loop engine ---

namespace {

// A 5x5 grid over two MODELLED demon-core axes -> 25 distinct cfgs / unit_ids.
const char* kRunnerGrid = R"(
name = "runner_grid"
base_scenario = "demon_core"
[sweep]
sampler = "grid"
budget_runs = 25
[objective]
kind = "sensitivity"
[[space]]
param = "compression.ratio"
axis_class = "numerical"
range = [2.0, 2.5]
[[space]]
param = "pit.mass_kg"
axis_class = "numerical"
range = [5.0, 7.0]
)";

// A fast deterministic stub evaluator (no real burst).
RunOutcome stub_eval(const ns::api::StudioConfig&) {
    RunOutcome o;
    o.tally.timing.generations = 3;  // mark that a run happened
    o.wall_s = 0.001;
    return o;
}

}  // namespace

TEST_CASE("run_sweep records each sampled point once", "[nukefarm]") {
    SweepManifest m = SweepManifest::parse(kRunnerGrid);
    ns::store::SweepStore store(":memory:");

    SweepProgress p = run_sweep(m, store, stub_eval);
    REQUIRE(p.total == 25);
    REQUIRE(p.ran == 25);
    REQUIRE(p.skipped == 0);
    REQUIRE(store.count() == 25);              // 25 distinct unit_ids
    REQUIRE(store.count_with_status(ns::store::status::kDone) == 25);
}

TEST_CASE("run_sweep resume skips done units and never double-counts", "[nukefarm]") {
    SweepManifest m = SweepManifest::parse(kRunnerGrid);
    ns::store::SweepStore store(":memory:");

    // Pre-seed 10 of the 25 units as already done (a partial prior run).
    std::vector<std::string> uids;
    {
        auto s = make_sampler(m);
        while (auto pt = s->next()) uids.push_back(ns::api::studio_unit_id(apply_point(*pt)));
    }
    REQUIRE(uids.size() == 25);
    for (std::size_t i = 0; i < 10; ++i) store.record_run({uids[i], "seed", "seed", 0.0});
    REQUIRE(store.count() == 10);

    // Resume: only the remaining 15 run; nothing is double-counted.
    SweepProgress p1 = run_sweep(m, store, stub_eval);
    REQUIRE(p1.total == 25);
    REQUIRE(p1.skipped == 10);
    REQUIRE(p1.ran == 15);
    REQUIRE(store.count() == 25);

    // Re-run the completed sweep: every unit is skipped, count is unchanged.
    SweepProgress p2 = run_sweep(m, store, stub_eval);
    REQUIRE(p2.total == 25);
    REQUIRE(p2.ran == 0);
    REQUIRE(p2.skipped == 25);
    REQUIRE(store.count() == 25);
}

TEST_CASE("the default evaluator runs a real burst keyed by studio_unit_id", "[nukefarm]") {
    // The runner skips by studio_unit_id computed UP FRONT; it must equal the
    // unit_id the run itself records, or resume would skip the wrong units.
    ns::api::StudioConfig cfg;
    cfg.eigen_batch = 300;  // keep the real-MC smoke fast
    cfg.compression_ratio = 2.4;
    cfg.pit_mass_kg = 6.5;
    const std::string uid = ns::api::studio_unit_id(cfg);

    const RunOutcome out = default_evaluator(cfg);
    REQUIRE(out.tally.timing.generations >= 1);  // a genuine burst ran
    REQUIRE(out.wall_s >= 0.0);

    const auto runj = nlohmann::json::parse(out.run_json);
    REQUIRE(runj.at("unit_id").get<std::string>() == uid);
}

// --- M5-T3-c: the filesystem work-queue ---

namespace {

// A case-unique temp queue dir (parallel-ctest safe); cleared before + after.
std::filesystem::path fresh_queue_dir(const char* name) {
    std::filesystem::path d = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(d);
    return d;
}

}  // namespace

TEST_CASE("the work queue moves a unit through pending claimed done", "[nukefarm]") {
    const auto dir = fresh_queue_dir("nukesim_queue_test_lifecycle");
    WorkQueue q(dir);
    REQUIRE(q.enqueue("u1", "payload1") == true);
    REQUIRE(q.enqueue("u1", "payload1-again") == false);  // re-enqueue of a pending unit
    REQUIRE(q.pending_count() == 1);

    auto item = q.claim(100.0);
    REQUIRE(item.has_value());
    REQUIRE(item->unit_id == "u1");
    REQUIRE(item->payload == "payload1-again");
    REQUIRE(q.pending_count() == 0);
    REQUIRE(q.claimed_count() == 1);

    q.complete("u1");
    REQUIRE(q.claimed_count() == 0);
    REQUIRE(q.done_count() == 1);

    // A unit already done is not re-enqueued.
    REQUIRE(q.enqueue("u1", "payload1") == false);
    REQUIRE(q.pending_count() == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("an empty queue claim returns nothing", "[nukefarm]") {
    const auto dir = fresh_queue_dir("nukesim_queue_test_empty");
    WorkQueue q(dir);
    REQUIRE_FALSE(q.claim(1.0).has_value());
    std::filesystem::remove_all(dir);
}

TEST_CASE("reclaim_stale requeues a lease older than the threshold", "[nukefarm]") {
    const auto dir = fresh_queue_dir("nukesim_queue_test_stale");
    WorkQueue q(dir);
    q.enqueue("u1", "p");
    REQUIRE(q.claim(100.0).has_value());  // leased at t=100
    REQUIRE(q.claimed_count() == 1);

    // Age 5 (< threshold 10) -> not stale.
    REQUIRE(q.reclaim_stale(105.0, 10.0) == 0);
    REQUIRE(q.claimed_count() == 1);
    REQUIRE(q.pending_count() == 0);

    // Age 20 (> threshold 10) -> reclaimed back to pending.
    REQUIRE(q.reclaim_stale(120.0, 10.0) == 1);
    REQUIRE(q.claimed_count() == 0);
    REQUIRE(q.pending_count() == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("an induced reclaim does not double-count in the store", "[nukefarm]") {
    const auto dir = fresh_queue_dir("nukesim_queue_test_reclaim");
    WorkQueue q(dir);
    ns::store::SweepStore store(":memory:");
    const std::string uid = "unit-reclaim";
    q.enqueue(uid, R"({"compression.ratio":2.3})");

    // Worker A claims at t=100 but stalls (never completes).
    auto a = q.claim(100.0);
    REQUIRE(a.has_value());
    REQUIRE(a->unit_id == uid);

    // The lease goes stale; a supervisor reclaims it back to pending.
    REQUIRE(q.reclaim_stale(200.0, 50.0) == 1);
    REQUIRE(q.pending_count() == 1);

    // Worker B claims the requeued unit, records it, and completes it.
    auto b = q.claim(200.0);
    REQUIRE(b.has_value());
    REQUIRE(b->unit_id == uid);
    REQUIRE(store.record_run({uid, b->payload, "tallyB", 1.0}) == true);  // first finisher wins
    q.complete(uid);
    REQUIRE(q.done_count() == 1);

    // Worker A wakes up late and records the SAME unit — the store's INSERT-OR-IGNORE
    // idempotency (M5-T2) makes it a NO-OP: no double-count.
    REQUIRE(store.record_run({uid, a->payload, "tallyA", 9.0}) == false);
    REQUIRE(store.count() == 1);
    auto row = store.get_run(uid);
    REQUIRE(row.has_value());
    REQUIRE(row->tally_json == "tallyB");  // B's result stands, A's late write ignored

    std::filesystem::remove_all(dir);
}

// --- M5-T3-d: the distributed worker + the stale-lease policy (ADR-020) ---

TEST_CASE("stale_threshold_s uses the fallback below 10 completed and 2x median above",
          "[nukefarm]") {
    ns::store::SweepStore few(":memory:");
    for (int i = 0; i < 9; ++i) few.record_run({"u" + std::to_string(i), "", "", 3.0});
    REQUIRE(stale_threshold_s(few, 99.0) == 99.0);  // < 10 completed -> the wall-clock fallback

    ns::store::SweepStore ten(":memory:");
    for (int i = 1; i <= 10; ++i)
        ten.record_run({"v" + std::to_string(i), "", "", static_cast<double>(i)});
    // last 10 wall_s = 1..10 -> median (5+6)/2 = 5.5 -> 2x = 11.
    REQUIRE(stale_threshold_s(ten, 99.0) == 11.0);
}

TEST_CASE("submit enqueues the sampler points by unit_id", "[nukefarm]") {
    SweepManifest m = SweepManifest::parse(kRunnerGrid);  // a 5x5 grid -> 25 distinct units
    const auto dir = fresh_queue_dir("nukesim_queue_test_submit");
    WorkQueue q(dir);
    ns::store::SweepStore store(":memory:");

    REQUIRE(submit(m, q, store) == 25);
    REQUIRE(q.pending_count() == 25);
    REQUIRE(submit(m, q, store) == 0);  // idempotent: the units are already enqueued

    std::filesystem::remove_all(dir);
}

TEST_CASE("run_worker drains the queue into the store", "[nukefarm]") {
    SweepManifest m = SweepManifest::parse(kRunnerGrid);
    const auto dir = fresh_queue_dir("nukesim_queue_test_worker");
    WorkQueue q(dir);
    ns::store::SweepStore store(":memory:");
    submit(m, q, store);
    REQUIRE(q.pending_count() == 25);

    const auto clock = []() { return 1000.0; };
    WorkerResult r = run_worker(q, store, stub_eval, clock, 600.0);
    REQUIRE(r.processed == 25);
    REQUIRE(r.reclaimed == 0);
    REQUIRE(store.count() == 25);
    REQUIRE(q.done_count() == 25);
    REQUIRE(q.pending_count() == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("run_worker reclaims a stale lease and runs the unit", "[nukefarm]") {
    const auto dir = fresh_queue_dir("nukesim_queue_test_worker_reclaim");
    WorkQueue q(dir);
    ns::store::SweepStore store(":memory:");
    q.enqueue("stale-unit", R"({"compression.ratio":2.2})");

    // A crashed worker claimed it at t=100 and never completed.
    REQUIRE(q.claim(100.0).has_value());
    REQUIRE(q.claimed_count() == 1);

    // A fresh worker at t=200 with a 50 s threshold: the stale lease (age 100) is
    // reclaimed, then claimed + evaluated + recorded + completed.
    const auto clock = []() { return 200.0; };
    WorkerResult r = run_worker(q, store, stub_eval, clock, 50.0);
    REQUIRE(r.reclaimed == 1);
    REQUIRE(r.processed == 1);
    REQUIRE(store.count() == 1);
    REQUIRE(store.is_done("stale-unit"));
    REQUIRE(q.done_count() == 1);

    std::filesystem::remove_all(dir);
}

// --- M5-T3-e: the nukefarm CLI handlers ---

TEST_CASE("the CLI submit-worker-status round-trip drives a sweep", "[nukefarm]") {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "nukesim_cli_test";
    fs::remove_all(base);
    fs::create_directories(base);
    const std::string sweep_toml = (base / "sweep.toml").string();
    const std::string queue_dir = (base / "queue").string();
    const std::string db = (base / "sweep.db").string();  // a real file: two connections share it

    // A 3x3 grid over two modelled axes -> 9 distinct units.
    {
        std::ofstream f(sweep_toml);
        f << "name = \"cli_grid\"\n"
             "base_scenario = \"demon_core\"\n"
             "[sweep]\nsampler = \"grid\"\nbudget_runs = 9\n"
             "[objective]\nkind = \"sensitivity\"\n"
             "[[space]]\nparam = \"compression.ratio\"\naxis_class = \"numerical\"\nrange = [2.0, 2.5]\n"
             "[[space]]\nparam = \"pit.mass_kg\"\naxis_class = \"numerical\"\nrange = [5.0, 7.0]\n";
    }

    REQUIRE(cli_submit(sweep_toml, queue_dir, db) == 9);

    const StatusReport before = cli_status(db, queue_dir);
    REQUIRE(before.store_done == 0);
    REQUIRE(before.queue_pending == 9);

    const WorkerResult w = cli_worker(queue_dir, db, 600.0, stub_eval);  // stub: no real burst
    REQUIRE(w.processed == 9);

    const StatusReport after = cli_status(db, queue_dir);
    REQUIRE(after.store_done == 9);
    REQUIRE(after.queue_pending == 0);
    REQUIRE(after.queue_done == 9);

    fs::remove_all(base);
}

TEST_CASE("the CLI status reports zero for a fresh store", "[nukefarm]") {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "nukesim_cli_test_empty";
    fs::remove_all(base);
    fs::create_directories(base);
    const std::string db = (base / "sweep.db").string();

    const StatusReport s = cli_status(db, "");  // no queue dir
    REQUIRE(s.store_done == 0);
    REQUIRE(s.queue_pending == 0);

    fs::remove_all(base);
}



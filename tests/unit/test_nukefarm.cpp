// M5-T3-a — the 03 §7 sweep.toml loader + the axis/objective enforcement
// (MAJ-35) + the 06 §2 samplers grid/lhs/random. Catch2 names are ASCII with no
// commas (a comma or em-dash breaks the ctest<->Catch2 filter — the M5-T1 trap).

#include "app/nukefarm/sweep.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
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

TEST_CASE("mcts is rejected as not yet available", "[nukefarm]") {
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
    SweepManifest m = SweepManifest::parse(doc);  // a valid manifest...
    REQUIRE_THROWS_AS(make_sampler(m), SweepError);  // ...but the sampler is M5-T4
}

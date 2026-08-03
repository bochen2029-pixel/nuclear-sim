// M3-T3-a: tally.json (03 §5) + the nine tally invariants.
//
// DoD: `tally_invariants` (03 §5, all 9) green. The 03 §5 illustrative example is
// a NORMATIVE fixture (QC-04 / 11 §4) and must satisfy all nine; it is read out of
// the spec markdown so it cannot drift. oracle.py §6 checks the same nine in
// Python from the constants TOML — the two agree without sharing code.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include "core/constants/constants.h"
#include "physics/tally/tally.h"
#include "spec_examples.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

using ns::physics::InvariantConstants;
using ns::physics::parse_tally_json;
using ns::physics::TallyResult;
using ns::physics::tally_invariants;
using ns::physics::to_json;
using Catch::Matchers::WithinAbs;

namespace {

// t_max_s is a scenario field (03 §4 [kinetics]); the 03 §5 tally fixture pairs
// with the 03 §4 scenario EXAMPLE (t_max_s = 5.0e-6), not trinity_canonical.toml
// (1.0e-6). Read it from that same example so the pair cannot drift.
double scenario_example_t_max_s() {
    const std::string toml = spec_examples::scenario_example();
    std::smatch match;
    const std::regex re(R"(t_max_s\s*=\s*([0-9.eE+\-]+))");
    if (!std::regex_search(toml, match, re)) {
        throw std::runtime_error("t_max_s not found in the 03 §4 scenario example");
    }
    return std::stod(match[1].str());
}

// Built from ns::consts — no magic numbers. Matches oracle.py §6 exactly:
//   phi = C-041, N_A = C-916, M_pit = C-102 mass_kg × 1000 g,
//   core Pu molar masses C-910/C-911/C-919.
InvariantConstants canonical_constants() {
    InvariantConstants c;
    c.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;         // C-041
    c.n_a = ns::consts::avogadro_constant;                      // C-916
    c.m_pit_g = ns::consts::od_pu_ga_core_mass_kg * 1000.0;     // C-102 (kg → g)
    c.m_pu239 = ns::consts::molar_mass_pu239;                   // C-910
    c.m_pu240 = ns::consts::molar_mass_pu240;                   // C-911
    c.m_pu241 = ns::consts::molar_mass_pu241;                   // C-919
    c.t_max_s = scenario_example_t_max_s();                     // 03 §4 [kinetics]
    return c;
}

std::vector<int> violated_numbers(const TallyResult& t, const InvariantConstants& c) {
    std::vector<int> nums;
    for (const auto& v : tally_invariants(t, c)) {
        nums.push_back(v.number);
    }
    return nums;
}

bool contains(const std::vector<int>& v, int n) {
    return std::find(v.begin(), v.end(), n) != v.end();
}

}  // namespace

TEST_CASE("the 03 section 5 example satisfies all nine invariants", "[tally]") {
    const TallyResult t = parse_tally_json(spec_examples::tally_example());
    const InvariantConstants c = canonical_constants();

    const auto violations = tally_invariants(t, c);
    std::string report;
    for (const auto& v : violations) {
        report += "\n  invariant " + std::to_string(v.number) + " (" + v.label +
                  ") measured=" + std::to_string(v.measured) + " tol=" + std::to_string(v.tol);
    }
    INFO("t_max_s (03 §4 example) = " << c.t_max_s << report);
    REQUIRE(violations.empty());

    // The two tight residuals are genuinely tight, not accidentally satisfied:
    // I3 ≈ 7.5e-8, I4 ≈ 3.8e-8 — both far below 1e-6. Recomputed here directly.
    const double i3 = std::abs(t.yield_kt * c.phi_kt / t.fissions_total - 1.0);
    REQUIRE_THAT(i3, WithinAbs(0.0, 1e-6));
    REQUIRE(i3 < 5e-7);

    const double pu_mass = t.fissions_by_isotope.get("Pu239") * c.m_pu239 +
                           t.fissions_by_isotope.get("Pu240") * c.m_pu240 +
                           t.fissions_by_isotope.get("Pu241") * c.m_pu241;
    const double i4 = std::abs(pu_mass / (c.n_a * c.m_pit_g) - t.burnup.pu_fraction);
    REQUIRE(i4 < 5e-7);
}

TEST_CASE("each invariant is detected when its tally is perturbed", "[tally]") {
    const InvariantConstants c = canonical_constants();
    const TallyResult base = parse_tally_json(spec_examples::tally_example());

    SECTION("1: fission mesh length no longer matches shell edges") {
        TallyResult t = base;
        t.fission_mesh.fissions.pop_back();
        REQUIRE(contains(violated_numbers(t, c), 1));
    }
    SECTION("2: shell fissions no longer sum to fissions_total") {
        TallyResult t = base;
        t.fission_mesh.fissions[0] *= 1.01;
        REQUIRE(contains(violated_numbers(t, c), 2));
    }
    SECTION("3: yield inconsistent with fissions_total and Phi_kt") {
        TallyResult t = base;
        t.yield_kt *= 1.001;
        REQUIRE(contains(violated_numbers(t, c), 3));
    }
    SECTION("4: pu_fraction inconsistent with the per-isotope Pu mass") {
        TallyResult t = base;
        t.burnup.pu_fraction *= 1.001;
        REQUIRE(contains(violated_numbers(t, c), 4));
    }
    SECTION("5: tamper share inconsistent with tamper_yield_fraction") {
        TallyResult t = base;
        t.burnup.tamper_yield_fraction += 0.01;
        REQUIRE(contains(violated_numbers(t, c), 5));
    }
    SECTION("6: by-isotope sum drifts from fissions_total") {
        TallyResult t = base;
        t.fissions_by_isotope.entries.emplace_back("Xe135", 1.0e21);
        REQUIRE(contains(violated_numbers(t, c), 6));
    }
    SECTION("7: too many generations for t_max_s") {
        TallyResult t = base;
        t.timing.generations = 10'000'000;
        REQUIRE(contains(violated_numbers(t, c), 7));
    }
    SECTION("8: population series carries a non-finite value") {
        TallyResult t = base;
        t.population_series.log10_N = {std::nan("")};
        REQUIRE(contains(violated_numbers(t, c), 8));
    }
    SECTION("9: yield_split no longer sums to yield_kt") {
        TallyResult t = base;
        t.yield_split.post_peak_kt += 1.0;
        REQUIRE(contains(violated_numbers(t, c), 9));
    }
}

TEST_CASE("tally json serialize is idempotent after parse", "[tally]") {
    const std::string once = to_json(parse_tally_json(spec_examples::tally_example()));
    const std::string twice = to_json(parse_tally_json(once));
    REQUIRE(once == twice);
    // The round-tripped tally still satisfies all nine invariants.
    REQUIRE(tally_invariants(parse_tally_json(once), canonical_constants()).empty());
}

TEST_CASE("parser rejects a tally missing a required field", "[tally]") {
    // 03 §5 invariant 6: burnup.pu_fraction and burnup.tamper_yield_fraction are
    // ALWAYS both present and never merged — a hard parse contract.
    nlohmann::ordered_json missing_tamper = nlohmann::ordered_json::parse(spec_examples::tally_example());
    missing_tamper["burnup"].erase("tamper_yield_fraction");
    REQUIRE_THROWS_AS(parse_tally_json(missing_tamper.dump()), std::runtime_error);

    nlohmann::ordered_json missing_total = nlohmann::ordered_json::parse(spec_examples::tally_example());
    missing_total.erase("fissions_total");
    REQUIRE_THROWS_AS(parse_tally_json(missing_total.dump()), std::runtime_error);
}

TEST_CASE("invariant 4 needs each core Pu isotope's own molar mass (QC-05)", "[tally]") {
    const TallyResult t = parse_tally_json(spec_examples::tally_example());
    const InvariantConstants c = canonical_constants();

    // Correct: each core Pu isotope with its OWN molar mass → passes.
    const double pu_mass_correct = t.fissions_by_isotope.get("Pu239") * c.m_pu239 +
                                   t.fissions_by_isotope.get("Pu240") * c.m_pu240 +
                                   t.fissions_by_isotope.get("Pu241") * c.m_pu241;
    const double i4_correct = std::abs(pu_mass_correct / (c.n_a * c.m_pit_g) - t.burnup.pu_fraction);
    REQUIRE(i4_correct <= 1e-6);

    // The QC-05 mistake: M(Pu-239) applied to ALL Pu fissions. The Pu-240 share
    // injects a residual (~5.4e-6 absolute here) that exceeds the 1e-6 tolerance,
    // so the invariant becomes unsatisfiable rather than merely inexact.
    const double all_pu = t.fissions_by_isotope.get("Pu239") + t.fissions_by_isotope.get("Pu240") +
                          t.fissions_by_isotope.get("Pu241");
    const double pu_mass_wrong = all_pu * c.m_pu239;
    const double i4_wrong = std::abs(pu_mass_wrong / (c.n_a * c.m_pit_g) - t.burnup.pu_fraction);
    INFO("i4 correct=" << i4_correct << " wrong=" << i4_wrong);
    REQUIRE(i4_wrong > 1e-6);
    REQUIRE(i4_wrong > 5.0 * i4_correct);
}

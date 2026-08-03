// M3-T3-b: the 02 §3 α-mode coupling loop.
//
// DoD: the loop (kinetics + hydro + eigen + TallySink) is toy-testable via an
// injectable EigenFn and produces a 03 §5 tally that satisfies all nine
// tally_invariants (M3-T3-a). The EigenFn here scripts a k(t) rise-then-fall — the
// stand-in for the fast4 transport the canonical bundle plugs in — so the loop,
// E6 quench, refresh cadence and Λ rescale run without fast4.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/constants/constants.h"
#include "core/geometry/geometry.h"
#include "physics/couple/couple.h"
#include "physics/tally/tally.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

using ns::geom::Layer;
using ns::geom::LayerStack;
using ns::physics::BurstReport;
using ns::physics::BurstTally;
using ns::physics::CoupleConfig;
using ns::physics::EigenFn;
using ns::physics::GenerationSample;
using ns::physics::run_burst;
using ns::physics::TallySink;

namespace {

// Toy eigen result: a two-region Pu/U-238 assembly. Fission shares Pu-239 0.75 /
// Pu-240 0.01 / U-238 0.24; production = ν̄·fissions (ν̄_Pu = 2.9, ν̄_U8 = 2.5).
// by_layer groups Pu into the pit shell and U-238 into the tamper shell.
ns::physics::EigenResult make_eigen(double k, double lambda_s = 1.0e-8) {
    ns::physics::EigenResult er;
    er.k = k;
    er.beta_eff = 0.006;
    er.lambda_s = lambda_s;
    er.source.by_isotope_fissions = {0.75, 0.01, 0.24};
    er.source.by_isotope = {2.9 * 0.75, 2.9 * 0.01, 2.5 * 0.24};
    er.source.by_layer = {2.9 * 0.75 + 2.9 * 0.01, 2.5 * 0.24};  // pit (Pu) / tamper (U-238)
    return er;
}

LayerStack toy_geometry() {
    return LayerStack({Layer{"pit", 4.585, 0, "SIM"}, Layer{"tamper", 11.43, 1, "SIM"}});
}

ns::physics::BurstContext toy_context() {
    ns::physics::BurstContext ctx;
    ctx.isotope_names = {"Pu239", "Pu240", "U238"};
    ctx.isotope_molar_mass_g = {ns::consts::molar_mass_pu239, ns::consts::molar_mass_pu240,
                                ns::consts::molar_mass_u238};
    ctx.core_pu_isotopes = {0, 1};
    ctx.tamper_isotope = 2;
    ctx.shell_edges_cm = {0.0, 4.585, 11.43};  // two radial shells
    ctx.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;
    ctx.n_a = ns::consts::avogadro_constant;
    ctx.m_pit_g = ns::consts::od_pu_ga_core_mass_kg * 1000.0;
    ctx.run_id = "toy_burst";
    ctx.non_canonical = true;
    return ctx;
}

// The same constants the sink used, so the invariant recompute reconciles to
// log-sum-exp float precision (« the 1e-6 tolerance). t_max_s is the burst's own.
ns::physics::InvariantConstants invariant_constants(const CoupleConfig& cfg) {
    ns::physics::InvariantConstants c;
    c.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;
    c.n_a = ns::consts::avogadro_constant;
    c.m_pit_g = ns::consts::od_pu_ga_core_mass_kg * 1000.0;
    c.m_pu239 = ns::consts::molar_mass_pu239;
    c.m_pu240 = ns::consts::molar_mass_pu240;
    c.m_pu241 = ns::consts::molar_mass_pu241;
    c.t_max_s = cfg.t_max_s;
    return c;
}

// Records every generation sample for behavioural assertions.
struct Recorder : TallySink {
    std::vector<GenerationSample> samples;
    void on_generation(const GenerationSample& g) override { samples.push_back(g); }
};

// A scripted EigenFn over a k schedule (clamped to the last entry), counting calls.
EigenFn scripted(const std::vector<double>& ks, int& calls, double lambda_s = 1.0e-8) {
    return [&ks, &calls, lambda_s](const LayerStack&) {
        const double k = ks[std::min<std::size_t>(static_cast<std::size_t>(calls), ks.size() - 1)];
        ++calls;
        return make_eigen(k, lambda_s);
    };
}

}  // namespace

TEST_CASE("a scripted burst quenches on E6 and yields a spec-valid 03 section 5 tally", "[couple]") {
    CoupleConfig cfg;
    cfg.e_f_mev = ns::consts::e_f_prompt_deposited;                 // C-040
    cfg.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;         // C-041
    cfg.lambda_s_initial = ns::consts::generation_time_uncompressed * 1e-9;  // C-030 ns→s
    cfg.eigen_refresh_gens = 10;
    cfg.quench_epsilon = ns::consts::quench_epsilon;     // C-909
    cfg.t_max_s = 5.0e-6;

    // k rises prompt-supercritical, then falls subcritical → the burst peaks and
    // decays (Fuchs–Nordheim); E6 quenches it four decades below F_peak.
    std::vector<double> ks = {1.40, 1.40, 1.40, 1.00, 0.55, 0.55, 0.55, 0.55};
    int calls = 0;
    const EigenFn eigen = scripted(ks, calls);

    BurstTally tally;
    const BurstReport report = run_burst(cfg, toy_context(), toy_geometry(), eigen, tally);

    REQUIRE(report.supercritical_reached);
    REQUIRE(report.quenched);                  // E6, not t_max
    REQUIRE(report.generations > 30);          // ran well past the peak
    REQUIRE(report.eigen_calls > 1);
    REQUIRE(report.fissions_total > 0.0);

    const auto violations = ns::physics::tally_invariants(tally.result(), invariant_constants(cfg));
    std::string report_str;
    for (const auto& v : violations) {
        report_str += "\n  invariant " + std::to_string(v.number) + " (" + v.label + ")";
    }
    INFO("gens=" << report.generations << " eigen_calls=" << report.eigen_calls
                 << " yield_kt=" << tally.result().yield_kt << report_str);
    REQUIRE(violations.empty());               // all nine hold on the produced tally

    // BLK-03: post-peak integration is wired (a k=1 termination could not).
    REQUIRE(tally.result().yield_split.post_peak_kt > 0.0);
    REQUIRE(tally.result().yield_kt > 0.0);
    // k readouts are ordered peak ≥ quench (rose then fell).
    REQUIRE(tally.result().k_eff.peak >= tally.result().k_eff.at_quench);
}

TEST_CASE("the loop refreshes the eigen on the integer-counter cadence", "[couple]") {
    // Constant k ⇒ q = 0 ⇒ no auto-halving; ratio = 1 ⇒ no motion trigger. So the
    // eigen is called once initially plus once per eigen_refresh_gens generations.
    CoupleConfig cfg;
    cfg.e_f_mev = ns::consts::e_f_prompt_deposited;
    cfg.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;
    cfg.eigen_refresh_gens = 10;
    cfg.compression_ratio = 1.0;               // hydro off
    cfg.t_max_s = 3.05e-7;                      // ~31 generations at Λ = 1e-8

    std::vector<double> ks = {1.40};           // constant
    int calls = 0;
    BurstTally sink;
    const BurstReport report = run_burst(cfg, toy_context(), toy_geometry(), scripted(ks, calls), sink);

    REQUIRE(report.max_q == 0.0);              // no k change ⇒ no auto-halve
    REQUIRE(report.eigen_calls == 1 + (report.generations - 1) / cfg.eigen_refresh_gens);
    REQUIRE(report.eigen_calls == calls);      // the closure counted the same calls
}

TEST_CASE("Tier-1 compression drives the E3b density rescale of Lambda", "[couple]") {
    // With compression on, Λ ∝ 1/ρ falls as the pit implodes. lambda_s = 0 from the
    // eigen forces the config/rescale path (no per-refresh reset), so Λ declines
    // monotonically over a long refresh interval.
    CoupleConfig cfg;
    cfg.e_f_mev = ns::consts::e_f_prompt_deposited;
    cfg.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;
    cfg.lambda_s_initial = 1.0e-8;
    cfg.eigen_refresh_gens = 1000;             // one long interval over the window
    cfg.compression_ratio = 2.2;               // C-060
    cfg.compression_t0_s = 0.0;
    cfg.compression_t_c_s = 1.0e-6;
    cfg.t_max_s = 5.0e-7;                       // ~50 generations, deep into compression

    std::vector<double> ks = {1.40};
    int calls = 0;
    Recorder rec;
    run_burst(cfg, toy_context(), toy_geometry(), scripted(ks, calls, /*lambda_s=*/0.0), rec);

    REQUIRE(rec.samples.size() > 10);
    const double lambda_first = rec.samples.front().lambda_s;
    const double lambda_last = rec.samples.back().lambda_s;
    INFO("lambda_first=" << lambda_first << " lambda_last=" << lambda_last);
    REQUIRE(lambda_first == cfg.lambda_s_initial);
    REQUIRE(lambda_last < 0.95 * lambda_first);  // compression shortened the generation time
}

TEST_CASE("a subcritical assembly never goes prompt-supercritical and does not quench", "[couple]") {
    // k = 0.9 ⇒ k_prompt ≈ 0.894 < 1 always: the population decays from N0, never
    // peaks, so E6 (which requires having gone supercritical) cannot fire.
    CoupleConfig cfg;
    cfg.e_f_mev = ns::consts::e_f_prompt_deposited;
    cfg.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;
    cfg.eigen_refresh_gens = 10;
    cfg.t_max_s = 2.0e-7;

    std::vector<double> ks = {0.90};
    int calls = 0;
    BurstTally sink;
    const BurstReport report = run_burst(cfg, toy_context(), toy_geometry(), scripted(ks, calls), sink);

    REQUIRE_FALSE(report.supercritical_reached);
    REQUIRE_FALSE(report.quenched);            // hit t_max, not E6
}

TEST_CASE("the streaming sink reconciles per-isotope and per-shell fissions to the total", "[couple]") {
    // Invariants 2 and 6 hold by construction of the log-sum-exp accumulation:
    // Σ shares == 1 each generation, so Σ isotope/shell fissions == total exactly.
    CoupleConfig cfg;
    cfg.e_f_mev = ns::consts::e_f_prompt_deposited;
    cfg.phi_kt = ns::consts::phi_kt_fissions_per_kiloton;
    cfg.eigen_refresh_gens = 10;
    cfg.quench_epsilon = ns::consts::quench_epsilon;
    cfg.t_max_s = 5.0e-6;

    std::vector<double> ks = {1.40, 1.40, 1.40, 1.00, 0.55, 0.55};
    int calls = 0;
    BurstTally tally;
    run_burst(cfg, toy_context(), toy_geometry(), scripted(ks, calls), tally);

    const auto& t = tally.result();
    const double total = t.fissions_total;
    REQUIRE(total > 0.0);

    double iso_sum = 0.0;
    for (const auto& [name, value] : t.fissions_by_isotope.entries) iso_sum += value;
    REQUIRE_THAT(iso_sum, Catch::Matchers::WithinRel(total, 1e-9));

    double shell_sum = 0.0;
    for (const double f : t.fission_mesh.fissions) shell_sum += f;
    REQUIRE_THAT(shell_sum + t.fission_mesh.leaked_fissions, Catch::Matchers::WithinRel(total, 1e-12));

    // Per-isotope split matches the production shares (Pu-239 dominant).
    REQUIRE(t.fissions_by_isotope.get("Pu239") > t.fissions_by_isotope.get("U238"));
    REQUIRE(t.fissions_by_isotope.get("U238") > t.fissions_by_isotope.get("Pu240"));
}

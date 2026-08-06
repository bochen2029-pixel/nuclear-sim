// M3-T1: α-mode burst kinetics (E3, 01 §4 / 05 §3).
//
// The DoD (05 §3): closed-form F_n at fixed k, renorm-invariance, weight-weighted
// F_n split, and a mixed-assembly ν̄_eff hand-check. The last two run a real ref
// eigen so ν̄_eff and the per-isotope split are checked against the SYNC-M3
// per-isotope source (production + fissions), not synthetic stand-ins.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/checkpoint/checkpoint.h"
#include "physics/eigen/eigen.h"
#include "physics/kinetics/kinetics.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "spec_examples.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using ns::geom::Layer;
using ns::geom::LayerStack;
using ns::physics::BurstAccumulator;

namespace {

/// A two-isotope sphere (Pu-239 ν̄=2.9, U-238 ν̄=2.5, both constant across groups
/// so the fission-weighted ν̄ is exactly reconstructible) built through the real
/// loaders, for the ν̄_eff / weight-weighted-split checks.
class MixedSphere {
public:
    MixedSphere() {
        root_ = fs::temp_directory_path() / ("nukesim_kin_" + std::to_string(counter()++));
        fs::remove_all(root_);
        fs::create_directories(root_ / "xs");
        fs::create_directories(root_ / "materials");

        const auto four = [](double v) { return json::array({v, v, v, v, v}); };
        const json identity = json::array({json::array({1.0, 0.0, 0.0, 0.0, 0.0}),
                                           json::array({0.0, 1.0, 0.0, 0.0, 0.0}),
                                           json::array({0.0, 0.0, 1.0, 0.0, 0.0}),
                                           json::array({0.0, 0.0, 0.0, 1.0, 0.0}), json::array({0.0, 0.0, 0.0, 0.0, 1.0})});
        const json pu = {{"nu", four(2.9)},  // constant across groups → ν̄_Pu = 2.9 exactly
                         {"chi", json::array({1.0, 0.0, 0.0, 0.0, 0.0})},
                         {"sigma_f", four(1.4)},
                         {"sigma_c", four(0.15)},
                         {"sigma_s", four(4.0)},
                         {"sigma_n2n", four(0.0)},
                         {"mu_bar", four(0.0)},
                         {"beta", 0.0020},
                         {"transfer", identity},
                         {"cite", "synthetic test medium — not physical data"},
                         {"status", "SIM"}};
        const json u8 = {{"nu", four(2.5)},  // constant across groups → ν̄_U = 2.5 exactly
                         {"chi", json::array({1.0, 0.0, 0.0, 0.0, 0.0})},
                         {"sigma_f", four(0.5)},
                         {"sigma_c", four(0.4)},
                         {"sigma_s", four(5.0)},
                         {"sigma_n2n", four(0.0)},
                         {"mu_bar", four(0.0)},
                         {"beta", 0.0157},
                         {"transfer", identity},
                         {"cite", "synthetic test medium — not physical data"},
                         {"status", "SIM"}};
        const json xs = {{"schema_version", 3},
                         {"name", "mix"},
                         {"group_bounds_MeV", json::array({20.0, 3.0, 1.0, 0.1, 1e-3, 1e-10})},
                         {"isotopes", {{"Pu239", pu}, {"U238", u8}}}};
        spec_examples::write_file(root_ / "xs" / "mix.json", xs.dump(2));

        const json mat = {{"schema_version", 1},
                          {"name", "fuel"},
                          {"density_g_cm3", 18.0},
                          {"status", "SIM"},
                          {"cite", "synthetic"},
                          {"isotopes", {{"Pu239", 0.7}, {"U238", 0.3}}}};
        spec_examples::write_file(root_ / "materials" / "fuel.json", mat.dump(2));

        xs_ = std::make_unique<ns::xs::FewGroupXS>(ns::xs::FewGroupXS::load(root_ / "xs" / "mix.json"));
        materials_ = std::make_unique<ns::material::MaterialLib>(
            ns::material::MaterialLib::load_dir(root_ / "materials", *xs_));
        stack_ = LayerStack({Layer{"core", 8.0, 0, "SIM"}});
    }
    ~MixedSphere() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    MixedSphere(const MixedSphere&) = delete;
    MixedSphere& operator=(const MixedSphere&) = delete;

    ns::ref::RefTransport transport() const {
        return ns::ref::RefTransport(stack_, *materials_, *xs_, 20260802);
    }

private:
    static int& counter() {
        static int v = 0;
        return v;
    }
    fs::path root_;
    std::unique_ptr<ns::xs::FewGroupXS> xs_;
    std::unique_ptr<ns::material::MaterialLib> materials_;
    LayerStack stack_;
};

}  // namespace

TEST_CASE("nu_eff and isotope shares from a per-isotope source (E3a)", "[kinetics]") {
    // Pure-function hand-check: production_i = ν̄_i·fissions_i, so
    // ν̄_eff = Σ production / Σ fissions is the fission-weighted mean ν̄.
    ns::ref::FissionSource s;
    s.by_isotope = {2.9 * 100.0, 2.4 * 50.0};        // production
    s.by_isotope_fissions = {100.0, 50.0};           // fissions
    // ν̄_eff = (290 + 120) / 150 = 410/150.
    REQUIRE_THAT(ns::physics::nu_eff(s), Catch::Matchers::WithinRel(410.0 / 150.0, 1e-12));

    const auto shares = ns::physics::isotope_fission_shares(s);
    REQUIRE(shares.size() == 2);
    REQUIRE_THAT(shares[0], Catch::Matchers::WithinRel(290.0 / 410.0, 1e-12));
    REQUIRE_THAT(shares[1], Catch::Matchers::WithinRel(120.0 / 410.0, 1e-12));
    REQUIRE_THAT(shares[0] + shares[1], Catch::Matchers::WithinAbs(1.0, 1e-12));

    // No fissions ⇒ ν̄_eff defined as 0 (avoids a 0/0).
    ns::ref::FissionSource empty;
    empty.by_isotope = {0.0};
    empty.by_isotope_fissions = {0.0};
    REQUIRE(ns::physics::nu_eff(empty) == 0.0);
}

TEST_CASE("rossi_alpha and refresh_q (E3c, R-13)", "[kinetics]") {
    REQUIRE_THAT(ns::physics::rossi_alpha(1.5, 1e-8),
                 Catch::Matchers::WithinRel(0.5 / 1e-8, 1e-12));
    REQUIRE(ns::physics::rossi_alpha(1.5, 0.0) == 0.0);  // Λ ≤ 0 guard

    // q = |Δk| / (k · gens). 0.03/(1.5·10) = 0.002.
    REQUIRE_THAT(ns::physics::refresh_q(0.03, 1.5, 10),
                 Catch::Matchers::WithinRel(0.002, 1e-12));
    REQUIRE(ns::physics::refresh_q(0.03, 1.5, 0) == 0.0);  // denominator guard
}

TEST_CASE("burst accumulator reproduces the fixed-k closed form (E3a)", "[kinetics]") {
    // k_prompt = 2, ν̄_eff = 2.9, S = 0, N_0 = 1 ⇒ N_n = 2^n, F_n = 2·N_n/2.9 =
    // 2^(n+1)/2.9, and F_cum = (2/2.9)(2^(n+1) − 1). The log-domain accumulator
    // must reproduce these exactly despite the exponential growth.
    constexpr double k = 2.0;
    constexpr double nu = 2.9;
    constexpr double e_f = 180.0;  // arbitrary here; E_cum = e_f·F_cum
    BurstAccumulator acc(1.0);

    for (int n = 0; n < 40; ++n) {
        acc.step(k, nu, e_f);
        // F_n just stepped = 2^(n+1)/2.9.
        const double f_n = std::pow(2.0, n + 1) / nu;
        REQUIRE_THAT(std::pow(10.0, acc.log10_fissions_last()),
                     Catch::Matchers::WithinRel(f_n, 1e-12));
        // N_{n+1} = 2^{n+1}.
        REQUIRE_THAT(acc.log10_N(),
                     Catch::Matchers::WithinAbs(static_cast<double>(n + 1) * std::log10(2.0), 1e-12));
    }
    // F_cum = (2/2.9)(2^40 − 1); E_cum = e_f·F_cum.
    const double f_cum = (2.0 / nu) * (std::pow(2.0, 40) - 1.0);
    REQUIRE_THAT(std::pow(10.0, acc.log10_fissions_cumulative()),
                 Catch::Matchers::WithinRel(f_cum, 1e-12));
    REQUIRE_THAT(acc.log10_energy_cumulative() - acc.log10_fissions_cumulative(),
                 Catch::Matchers::WithinAbs(std::log10(e_f), 1e-12));
    // Yield in the log domain: log10(F_cum/Φ_kt).
    REQUIRE_THAT(acc.log10_yield_kt(1.4508041e23),
                 Catch::Matchers::WithinAbs(std::log10(f_cum) - std::log10(1.4508041e23), 1e-12));
}

TEST_CASE("renormalization does not perturb the cumulants (renorm x3 invariance)", "[kinetics]") {
    // 05 §3: log-renormalization with cumulants unaffected. Two identical 50-gen
    // runs; one is renormalized 3 times mid-run. Every reported quantity must
    // match to float precision (the renorm rescales the mantissa, whose log then
    // recombines with the offset — invariant up to ~1 ULP/step, never a physical
    // shift).
    constexpr double k = 2.0;
    constexpr double nu = 2.9;
    constexpr double e_f = 180.0;
    BurstAccumulator plain(1.0);
    BurstAccumulator renorm(1.0);

    for (int n = 0; n < 50; ++n) {
        plain.step(k, nu, e_f);
        renorm.step(k, nu, e_f);
        if (n == 10 || n == 25 || n == 40) {
            renorm.renormalize();
        }
    }
    REQUIRE_THAT(renorm.log10_fissions_cumulative(),
                 Catch::Matchers::WithinAbs(plain.log10_fissions_cumulative(), 1e-11));
    REQUIRE_THAT(renorm.log10_fissions_last(),
                 Catch::Matchers::WithinAbs(plain.log10_fissions_last(), 1e-11));
    REQUIRE_THAT(renorm.log10_N(), Catch::Matchers::WithinAbs(plain.log10_N(), 1e-11));
    REQUIRE_THAT(renorm.log10_yield_kt(1.4508041e23),
                 Catch::Matchers::WithinAbs(plain.log10_yield_kt(1.4508041e23), 1e-11));
}

TEST_CASE("burst accumulator applies the initiator source term S_n (E3a)", "[kinetics]") {
    // k_prompt = 1, S = 1 each generation, N_0 = 1 ⇒ N_n = n + 1 (linear
    // accumulation from the initiator). Exercises the S_next path of E3a.
    BurstAccumulator acc(1.0);
    for (int n = 1; n <= 20; ++n) {
        acc.step(1.0, 2.9, 180.0, 1.0);
        REQUIRE_THAT(acc.log10_N(),
                     Catch::Matchers::WithinRel(std::log10(static_cast<double>(n + 1)), 1e-12));
    }
}

TEST_CASE("nu_eff and the F_n split from a real mixed-assembly eigen (E3a)", "[kinetics]") {
    // A real Pu-239/U-238 eigen. The SYNC-M3 per-isotope source must satisfy
    // production_i = ν̄_i·fissions_i per isotope (2.9 for Pu, 2.5 for U-238), so
    // ν̄_eff = Σ production/Σ fissions is the fission-weighted mean, bracketed by
    // the two ν̄'s. The F_n split (isotope_fission_shares) is the production share
    // and sums to 1.
    const MixedSphere w;
    ns::ref::RefTransport t = w.transport();
    ns::physics::EigenSpec spec;
    spec.batch = 4000;
    spec.inactive = 8;
    spec.active = 15;
    spec.h_tol = 0.05;
    const ns::physics::EigenResult er = ns::physics::run_eigen(t, spec);

    const ns::ref::FissionSource& s = er.source;
    REQUIRE(s.by_isotope.size() == s.by_isotope_fissions.size());

    // Per isotope: production / fissions == that isotope's ν̄ (2.9 or 2.5).
    int fissioning = 0;
    for (std::size_t i = 0; i < s.by_isotope.size(); ++i) {
        if (s.by_isotope_fissions[i] > 0.0) {
            ++fissioning;
            const double nubar_i = s.by_isotope[i] / s.by_isotope_fissions[i];
            const bool is_pu = std::abs(nubar_i - 2.9) < 1e-6;
            const bool is_u8 = std::abs(nubar_i - 2.5) < 1e-6;
            INFO("isotope " << i << " nubar_i=" << nubar_i);
            REQUIRE((is_pu || is_u8));
        }
    }
    REQUIRE(fissioning == 2);  // both Pu-239 and U-238 fission

    // ν̄_eff is the fission-weighted mean, strictly between 2.5 and 2.9.
    const double neff = ns::physics::nu_eff(s);
    INFO("nu_eff=" << neff);
    REQUIRE(neff > 2.5);
    REQUIRE(neff < 2.9);
    // And it equals Σ production / Σ fissions exactly (definition, recomputed).
    double prod = 0.0;
    double fiss = 0.0;
    for (std::size_t i = 0; i < s.by_isotope.size(); ++i) {
        prod += s.by_isotope[i];
        fiss += s.by_isotope_fissions[i];
    }
    REQUIRE_THAT(neff, Catch::Matchers::WithinRel(prod / fiss, 1e-12));

    // The per-isotope F_n split (production shares) sums to 1.
    const auto shares = ns::physics::isotope_fission_shares(s);
    double sum = 0.0;
    for (const double sh : shares) {
        sum += sh;
    }
    REQUIRE_THAT(sum, Catch::Matchers::WithinAbs(1.0, 1e-12));
}

TEST_CASE("a BurstAccumulator resumes BIT-IDENTICALLY from a checkpoint (M5-T1-b)", "[kinetics]") {
    // The α-burst's core stochastic state is the crux of the T-resume gate. Step an
    // accumulator through a supercritical stretch (the log-domain state spans many
    // decades), checkpoint it THROUGH the M5-T1-a container (03 §8), restore, and
    // continue: the restored accumulator must match the uninterrupted one bit-for-bit
    // (==, not approx) at every subsequent generation.
    BurstAccumulator original(1.0);
    const double nu = 2.9, e_f = 180.0;
    for (int i = 0; i < 40; ++i) {
        original.step(1.4, nu, e_f);  // ~40 decades of growth
    }

    // Serialize the state → a checkpoint section (03 §8 §7) → write → read → restore.
    ns::checkpoint::CheckpointBlob blob;
    blob.identity.scenario_sha256 = std::string(64, '0');
    blob.identity.data_sha256 = std::string(64, '0');
    blob.put_section(7, ns::physics::serialize_accumulator_state(original.state()));
    const std::vector<std::uint8_t> bytes = ns::checkpoint::write_checkpoint(blob);
    const ns::checkpoint::CheckpointBlob rd = ns::checkpoint::read_checkpoint(bytes);
    const ns::checkpoint::CheckpointSection* sec = rd.section(7);
    REQUIRE(sec != nullptr);
    BurstAccumulator restored =
        BurstAccumulator::from_state(ns::physics::deserialize_accumulator_state(sec->data));

    // The restore is immediately identical (every readout, including the full history).
    REQUIRE(restored.generations() == original.generations());
    REQUIRE(restored.log10_N() == original.log10_N());
    REQUIRE(restored.log10_fissions_last() == original.log10_fissions_last());
    REQUIRE(restored.log10_fissions_cumulative() == original.log10_fissions_cumulative());
    REQUIRE(restored.log10_energy_cumulative() == original.log10_energy_cumulative());
    REQUIRE(restored.log10_N_history() == original.log10_N_history());

    // And it stays bit-identical as BOTH continue through the peak, decay, and quench.
    for (const double k : {1.4, 1.0, 0.6, 0.6, 0.6}) {
        original.step(k, nu, e_f);
        restored.step(k, nu, e_f);
        REQUIRE(restored.log10_N() == original.log10_N());
        REQUIRE(restored.log10_fissions_last() == original.log10_fissions_last());
        REQUIRE(restored.log10_fissions_cumulative() == original.log10_fissions_cumulative());
    }
    REQUIRE(restored.generations() == original.generations());
    REQUIRE(restored.log10_N_history() == original.log10_N_history());
}

// M0-T3-a: the generated constants header (03 §1, 01 §8).
//
// Including the header is itself most of the test: every banded constant
// carries a generated `static_assert(lo <= value <= hi)`, so a band violation
// anywhere in the 96-entry table fails to compile rather than failing here.
// What remains are the checks a static_assert cannot make.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/constants/constants_generated.h"

namespace k = nukesim::constants;

TEST_CASE("derived Phi_kt is recomputed, not transcribed", "[constants]") {
    // C-041 is the only constant converting fissions to yield (E6). Recompute
    // it here from its inputs rather than trusting the generator that emitted
    // it — this is the smallest instance of the 11 §5 oracle principle: a
    // number checked a second, independent way.
    const double expected = k::kiloton_tnt_to_joule
                          / (k::e_f_prompt_deposited * k::mev_to_joule);

    REQUIRE_THAT(k::phi_kt_fissions_per_kiloton,
                 Catch::Matchers::WithinRel(expected, 1e-12));
    // The appendix prints it rounded to 1.4508e23; the computed value is
    // 1.4508041e23. Guard the magnitude so a units slip cannot hide.
    REQUIRE(k::phi_kt_fissions_per_kiloton > 1.45e23);
    REQUIRE(k::phi_kt_fissions_per_kiloton < 1.46e23);
}

TEST_CASE("nu-bar constants are TOTAL, not prompt", "[constants]") {
    // ADR-013. If someone silently swaps in prompt-only data, nu-bar drops by
    // roughly beta. These bounds are loose enough to be convention checks
    // rather than data checks, and tight enough to catch that swap.
    REQUIRE(k::nu_bar_pu239_total > 2.8);
    REQUIRE(k::nu_bar_u235_total > 2.3);

    // beta is REQUIRED per isotope precisely so k_prompt can be derived once,
    // downstream (ADR-013). All five must exist and be plausible fractions.
    for (const double beta : {k::beta_pu239, k::beta_u235, k::beta_u238,
                              k::beta_pu240, k::beta_pu241}) {
        REQUIRE(beta > 0.0);
        REQUIRE(beta < 0.02);
    }
    // U-235's beta is the one that made the double-count visible: ~650 pcm,
    // larger than G0a's entire tolerance.
    REQUIRE(k::beta_u235 * 1e5 > k::g0_k_deviation_tolerance);
}

TEST_CASE("canonical geometry radii are strictly nested", "[constants]") {
    // 04 §7's LayerStack requires strictly increasing outer radii. Catching a
    // transposed digit here costs nothing; catching it in M1-T1 costs a day.
    const double od[] = {
        k::od_urchin_initiator,   k::od_initiator_cavity, k::od_pu_ga_core,
        k::od_natural_u_tamper,   k::od_b10_acrylic_shell, k::od_aluminum_pusher,
        k::od_inner_he_booster,   k::od_lens_he_layer,     k::od_cork_liner,
        k::od_duralumin_case,
    };
    for (std::size_t i = 1; i < std::size(od); ++i) {
        REQUIRE(od[i] > od[i - 1]);
    }
}

TEST_CASE("critical mass scales as rho^-2 across the phase pair", "[constants]") {
    // C-050 (10 kg at 19.8) and C-050b (16-17 kg at 15.6) are the same physics
    // at two densities, not a contradiction (MAJ-29). Verify that claim rather
    // than repeating it: 10 * (19.8/15.6)^2 ~ 16.1 kg, inside C-050b's band.
    constexpr double rho_alpha = 19.8;
    constexpr double rho_delta = 15.6;
    const double scaled = k::critical_mass_bare_pu239_alpha
                        * (rho_alpha / rho_delta) * (rho_alpha / rho_delta);

    REQUIRE(scaled > k::critical_mass_bare_pu_delta_lo);
    REQUIRE(scaled < k::critical_mass_bare_pu_delta_hi);
}

TEST_CASE("RNG stream ids are the seven distinct values 04 section 2 fixes", "[constants]") {
    // Stream ids are part of the RNG's normative layout: a collision would
    // silently correlate two physical processes.
    const int streams[] = {
        k::rng_stream_registry::source,    k::rng_stream_registry::flight,
        k::rng_stream_registry::collision, k::rng_stream_registry::fission,
        k::rng_stream_registry::scatter,   k::rng_stream_registry::hydro,
        k::rng_stream_registry::render,
    };
    for (std::size_t i = 0; i < std::size(streams); ++i) {
        REQUIRE(streams[i] == static_cast<int>(i) + 1);
    }
}

TEST_CASE("gate bands are ordered and the G2 yield band spans the estimates", "[constants]") {
    // C-940 is the envelope of C-091/092/093 including Selby's +/-2. If a band
    // edge is edited without re-deriving it, the published estimates fall out.
    REQUIRE(k::g2_yield_band_lo <= k::trinity_yield_radiochemistry_1945);
    REQUIRE(k::g2_yield_band_hi >= k::trinity_yield_selby_2021_hi);
    REQUIRE(k::g2_yield_band_lo < k::trinity_yield_doe_official);
    REQUIRE(k::trinity_yield_doe_official < k::g2_yield_band_hi);
}

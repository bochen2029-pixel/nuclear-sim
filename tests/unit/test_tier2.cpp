// M3-T2: Tier-2 hydro (E4 / 01 §5, 05 §4).
//
// DoD (05 §4): energy conservation < 1e-6 over 1e4 RK4 steps (Ė_dep=0), no-motion,
// Guderley constructor matching, and α_G/γ consistency. The energy-conservation
// test is what exposed the E4 sign defect (see tier2.h / ADR-019).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/constants/constants.h"
#include "physics/hydro/tier2.h"

#include <cmath>

using ns::physics::GuderleyTiming;
using ns::physics::ShellState;
using ns::physics::SnowplowShell;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Guderley constructor matches r_lens_inner and v_in (derived t_c)", "[tier2]") {
    const double r_s0 = 5.0;      // r_lens_inner (cm)
    const double v_in = 2000.0;   // inward speed (cm/s in these units)
    const double alpha_g = ns::consts::guderley_alpha_spherical_gamma_5_3;  // C-070
    const GuderleyTiming g = GuderleyTiming::from_launch(r_s0, v_in, 0.0, alpha_g);

    // 05 §4 constructor DoD: R_s(t0)=r_s0, Ṙ_s(t0)=−v_in.
    REQUIRE_THAT(g.R_s(0.0), WithinAbs(r_s0, 1e-9));
    REQUIRE_THAT(std::abs(g.Rdot_s(0.0) + v_in) / v_in, WithinAbs(0.0, 1e-9));
    REQUIRE(g.Rdot_s(0.0) < 0.0);           // inward
    REQUIRE(g.t_c_s > 0.0);                  // derived, positive
    // The shock converges: R_s shrinks as t advances toward t_c.
    REQUIRE(g.R_s(0.5 * g.t_c_s) < r_s0);
}

TEST_CASE("alpha_G is the spherical gamma=5/3 Guderley exponent (C-070)", "[tier2]") {
    const double alpha_g = ns::consts::guderley_alpha_spherical_gamma_5_3;
    REQUIRE_THAT(alpha_g, WithinRel(0.688377, 1e-9));  // C-070
    REQUIRE(alpha_g > 0.6);
    REQUIRE(alpha_g < 0.7);
}

TEST_CASE("Tier-2 snowplow conserves E_int + 1/2 M Rdot^2 (Edot_dep=0, RK4)", "[tier2]") {
    // Disassembly: no deposition, no drive, expanding shell (Rdot>0). The interior
    // gas does work expanding — E_int falls, KE rises — with the TOTAL invariant
    // held to RK4 tolerance. (The pre-M3-T2 (P_drive - P_int) sign would drift.)
    const SnowplowShell shell{/*mass=*/1.0, /*gamma=*/5.0 / 3.0, /*p_drive=*/0.0, /*edot_dep=*/0.0};
    ShellState s{/*R=*/1.0, /*Rdot=*/1.0, /*E_int=*/1.0};

    const double e0 = shell.total_energy(s);
    const double eint0 = s.E_int;
    const double dt = 1.0e-4;
    for (int i = 0; i < 10000; ++i) {
        s = shell.rk4_step(s, dt);
    }
    const double e1 = shell.total_energy(s);
    INFO("E0=" << e0 << " E1=" << e1 << " R=" << s.R << " Rdot=" << s.Rdot << " Eint=" << s.E_int);

    REQUIRE_THAT(std::abs(e1 - e0) / e0, WithinAbs(0.0, 1e-6));  // conserved
    REQUIRE(s.R > 1.0);          // expanded
    REQUIRE(s.Rdot > 1.0);       // accelerated (gas pressure drove it out)
    REQUIRE(s.E_int < eint0);    // internal energy spent on expansion
    REQUIRE(s.E_int > 0.0);      // still positive
}

TEST_CASE("Tier-2 with no energy and no motion stays put", "[tier2]") {
    // Rdot(0)=0, E_int=0 (so P_int=0), Edot_dep=0, no drive ⇒ nothing moves.
    const SnowplowShell shell{1.0, 5.0 / 3.0, 0.0, 0.0};
    ShellState s{2.5, 0.0, 0.0};
    for (int i = 0; i < 1000; ++i) {
        s = shell.rk4_step(s, 1.0e-3);
    }
    REQUIRE_THAT(s.R, WithinAbs(2.5, 1e-12));
    REQUIRE_THAT(s.Rdot, WithinAbs(0.0, 1e-12));
    REQUIRE_THAT(s.E_int, WithinAbs(0.0, 1e-12));
}

TEST_CASE("Tier-2 P_drive does work on the invariant (sign sanity)", "[tier2]") {
    // With P_drive>0 compressing an expanding shell, d/dt(E_int+KE) = -P_drive*dV/dt
    // < 0 while dV/dt>0: the invariant DECREASES (drive removes shell energy). This
    // pins the corrected sign — the wrong sign would change this direction.
    const SnowplowShell shell{1.0, 5.0 / 3.0, /*p_drive=*/0.05, /*edot_dep=*/0.0};
    ShellState s{1.0, 1.0, 1.0};
    const double e0 = shell.total_energy(s);
    for (int i = 0; i < 2000; ++i) {
        s = shell.rk4_step(s, 1.0e-4);
    }
    REQUIRE(s.R > 1.0);                              // still expands initially (Rdot>0)
    REQUIRE(shell.total_energy(s) < e0);            // drive did negative work on the shell
}

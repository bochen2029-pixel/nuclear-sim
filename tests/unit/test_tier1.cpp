// M2-T2: Tier-1 parametric compression (01 §5 / 05 §4).
//
// DoD: endpoints to 1e-12 AND mass conservation at s ∈ {0,.25,.5,.75,1} — the
// BLK-01-strength test (the original spec had a compression that did not conserve
// mass). smootherstep and the LayerStack compress helper are covered too.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/geometry/geometry.h"
#include "physics/hydro/tier1.h"

#include <cmath>

using ns::physics::Tier1Compression;
using ns::physics::compress;
using ns::physics::smootherstep;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("smootherstep endpoints, midpoint, symmetry, clamping, flat ends", "[tier1]") {
    REQUIRE(smootherstep(0.0) == 0.0);
    REQUIRE(smootherstep(1.0) == 1.0);
    REQUIRE_THAT(smootherstep(0.5), WithinAbs(0.5, 1e-12));
    // Symmetric about (0.5, 0.5): s(u) + s(1-u) = 1.
    for (const double u : {0.1, 0.25, 0.4}) {
        REQUIRE_THAT(smootherstep(u) + smootherstep(1.0 - u), WithinAbs(1.0, 1e-12));
    }
    // Clamped outside [0,1].
    REQUIRE(smootherstep(-0.5) == 0.0);
    REQUIRE(smootherstep(1.5) == 1.0);
    // Zero 1st/2nd derivative at the ends ⇒ still ~0 (resp. ~1) very close in.
    REQUIRE(smootherstep(1e-4) < 1e-11);
    REQUIRE(smootherstep(1.0 - 1e-4) > 1.0 - 1e-11);
}

TEST_CASE("Tier-1 endpoints hold to 1e-12", "[tier1]") {
    // r(s=0) = r_0 and r(s=1) = r_0*(ratio)^(-1/3), density 1 -> ratio.
    for (const double ratio : {2.2, 2.5, 1.5}) {  // 2.2 = C-060
        const Tier1Compression comp{ratio, 0.0, 1.0e-6};
        REQUIRE_THAT(comp.radius_scale(0.0), WithinAbs(1.0, 1e-12));
        REQUIRE_THAT(comp.radius_scale(1.0), WithinAbs(std::pow(ratio, -1.0 / 3.0), 1e-12));
        REQUIRE_THAT(comp.density_ratio(0.0), WithinAbs(1.0, 1e-12));
        REQUIRE_THAT(comp.density_ratio(1.0), WithinRel(ratio, 1e-12));
    }
}

TEST_CASE("Tier-1 conserves mass at s in {0,.25,.5,.75,1} (BLK-01 test)", "[tier1]") {
    // mass/mass_0 = (rho/rho_0)*(r/r_0)^3 = density_ratio * radius_scale^3, which
    // MUST be 1 at every s for uniform compression. The original BLK-01 formula
    // failed exactly this.
    const Tier1Compression comp{2.2, 0.0, 1.0e-6};  // C-060
    for (const double s : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const double rs = comp.radius_scale(s);
        const double mass_rel = comp.density_ratio(s) * rs * rs * rs;
        INFO("s=" << s << " radius_scale=" << rs);
        REQUIRE_THAT(mass_rel, WithinAbs(1.0, 1e-12));
    }
}

TEST_CASE("Tier-1 s_of(t) ramps 0->1 over [t0, t0+t_c]", "[tier1]") {
    const Tier1Compression comp{2.2, 1.0e-6, 2.0e-6};  // starts at 1 us, lasts 2 us
    REQUIRE(comp.s_of(0.0) == 0.0);                     // before t0
    REQUIRE(comp.s_of(1.0e-6) == 0.0);                  // at t0
    REQUIRE_THAT(comp.s_of(2.0e-6), WithinAbs(0.5, 1e-12));  // midpoint -> smootherstep(0.5)
    REQUIRE(comp.s_of(3.0e-6) == 1.0);                  // at t0 + t_c
    REQUIRE(comp.s_of(5.0e-6) == 1.0);                  // after (clamped)
}

TEST_CASE("compress scales every layer radius uniformly and preserves nesting", "[tier1]") {
    const ns::geom::LayerStack stack({{"pit", 4.585, 0, "SIM"},
                                      {"tamper", 11.43, 1, "SIM"},
                                      {"pusher", 23.495, 2, "SIM"}});
    const Tier1Compression comp{2.2, 0.0, 1.0e-6};  // C-060
    const double scale = comp.radius_scale(1.0);     // full compression
    const ns::geom::LayerStack c = compress(stack, scale);

    REQUIRE(c.size() == stack.size());
    for (int i = 0; i < stack.size(); ++i) {
        INFO("layer " << i);
        REQUIRE_THAT(c.layer(i).r_outer, WithinRel(stack.layer(i).r_outer * scale, 1e-12));
        // material / name / status carried through.
        REQUIRE(c.layer(i).material_id == stack.layer(i).material_id);
    }
    // Uniform scaling preserves radius ratios (and hence strict nesting).
    REQUIRE_THAT(c.layer(1).r_outer / c.layer(0).r_outer,
                 WithinRel(stack.layer(1).r_outer / stack.layer(0).r_outer, 1e-12));
    // Whole-pit mass conserved: density_ratio * scale^3 = 1.
    REQUIRE_THAT(comp.density_ratio(1.0) * scale * scale * scale, WithinAbs(1.0, 1e-12));
}
